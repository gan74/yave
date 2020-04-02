/*******************************
Copyright (c) 2016-2020 Grégoire Angerand

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
**********************************/
#ifndef YAVE_ASSETS_ASSETLOADER_H
#define YAVE_ASSETS_ASSETLOADER_H

#include <yave/device/DeviceLinked.h>
#include <yave/utils/serde.h>

#include "AssetPtr.h"
#include "AssetStore.h"

#include <y/concurrent/StaticThreadPool.h>
#include <y/serde3/archives.h>

#include <unordered_map>
#include <typeindex>
#include <mutex>
#include <future>

namespace yave {

class AssetLoader : NonCopyable, public DeviceLinked {
	public:
		enum class ErrorType {
			InvalidID,
			UnknownID,
			InvalidData,
			UnknownType,
			Unknown
		};

		template<typename T>
		using Result = core::Result<AssetPtr<T>, ErrorType>;

	private:
		class LoaderBase : NonCopyable {
			public:
				virtual ~LoaderBase();

				virtual AssetType type() const = 0;
				virtual bool forget(AssetId id) = 0;
		};

		template<typename T>
		class Loader final : public LoaderBase {
			using traits = AssetTraits<T>;
			static_assert(traits::is_asset, "Type is missing asset traits");

			public:
				Result<T> set(AssetId id, T&& asset) {
					y_profile();
					if(!id) {
						return core::Err(ErrorType::InvalidID);
					}

					const auto lock = y_profile_unique_lock(_lock);
					auto& weak_ptr = _loaded[id];
					AssetPtr asset_ptr = weak_ptr.lock();
					weak_ptr = (asset_ptr ? asset_ptr._ptr->reloaded : asset_ptr) = make_asset_with_id<T>(id, std::move(asset));

					return core::Ok(asset_ptr);
				}

				Result<T> load(AssetLoader& loader, AssetId id) noexcept {
					y_profile();
					if(id == AssetId::invalid_id()) {
						return core::Err(ErrorType::InvalidID);
					}

					const auto lock = y_profile_unique_lock(_lock);
					auto& weak_ptr = _loaded[id];
					AssetPtr asset_ptr = weak_ptr.lock();
					if(asset_ptr) {
						return core::Ok(asset_ptr);
					}

					if(auto reader = loader.store().data(id)) {
						y_profile_zone("loading");
						serde3::ReadableArchive arc(std::move(reader.unwrap()));
						typename traits::load_from load_from;
						if(!arc.deserialize(load_from, loader)) {
							return core::Err(ErrorType::InvalidData);
						}
						weak_ptr = asset_ptr = make_asset_with_id<T>(id, loader.device(), std::move(load_from));
						return core::Ok(std::move(asset_ptr));
					}

					return core::Err(ErrorType::Unknown);
				}

				AssetType type() const override {
					return traits::type;
				}

				bool forget(AssetId id) override {
					const auto lock = y_profile_unique_lock(_lock);
					if(const auto it = _loaded.find(id); it != _loaded.end()) {
						_loaded.erase(it);
						return true;
					}
					return false;
				}

			private:
				std::unordered_map<AssetId, WeakAssetPtr<T>> _loaded;

				std::mutex _lock;
		};

   public:
		AssetLoader(DevicePtr dptr, const std::shared_ptr<AssetStore>& store);
		~AssetLoader();

		AssetStore& store();
		const AssetStore& store() const;

		bool forget(AssetId id);


		template<typename T>
		std::future<Result<T>> load_async(AssetId id) {
			return _thread.schedule_with_future([this, id]() -> Result<T> { return load<T>(id); });
		}

		template<typename T>
		Result<T> load(AssetId id) {
			return loader_for_type<T>().load(*this, id);
		}

		template<typename T>
		Result<T> load(std::string_view name) {
			return load<T>(store().id(name));
		}

		template<typename T>
		Result<T> import(std::string_view name, std::string_view import_from) {
			return load<T>(load_or_import(name, import_from, AssetTraits<T>::type));
		}

		template<typename T>
		Result<T> set(AssetId id, T asset) {
			return loader_for_type<T>().set(id, std::move(asset));
		}

   private:
		template<typename T, typename E>
		Result<T> load(core::Result<AssetId, E> id) {
			if(id) {
				return load<T>(id.unwrap());
			}
			return core::Err(ErrorType::UnknownID);
		}

		LoaderBase* loader_for_id(AssetId id) {
			if(const auto type = _store->asset_type(id)) {
				const auto lock = y_profile_unique_lock(_lock);
				for(const auto& [index, loader] : _loaders) {
					unused(index);
					if(loader->type() == type.unwrap()) {
						return loader.get();
					}
				}
			}
			return nullptr;
		}

		template<typename T>
		auto& loader_for_type() {
			const auto lock = y_profile_unique_lock(_lock);
			using Type = remove_cvref_t<T>;
			auto& loader = _loaders[typeid(Type)];
			if(!loader) {
				loader = std::make_unique<Loader<Type>>();
			}
			return *dynamic_cast<Loader<Type>*>(loader.get());
		}



		core::Result<AssetId> load_or_import(std::string_view name, std::string_view import_from, AssetType type);

		std::unordered_map<std::type_index, std::unique_ptr<LoaderBase>> _loaders;
		std::shared_ptr<AssetStore> _store;

		std::mutex _lock;

		concurrent::WorkerThread _thread;
};

template<typename T>
void AssetPtr<T>::post_deserialize(AssetLoader& loader)  {
	if(auto r = loader.load<T>(_id)) {
		_ptr = r.unwrap()._ptr;
	}
}

}

#endif // YAVE_ASSETS_ASSETLOADER_H
