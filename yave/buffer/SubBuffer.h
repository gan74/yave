/*******************************
Copyright (c) 2016-2017 Grégoire Angerand

This code is licensed under the MIT License (MIT).

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
#ifndef YAVE_BUFFER_SUBBUFFER_H
#define YAVE_BUFFER_SUBBUFFER_H

#include "SubBufferBase.h"
#include "Buffer.h"

namespace yave {

template<BufferUsage Usage, MemoryFlags Flags = prefered_memory_flags<Usage>(), BufferTransfer Transfer = prefered_transfer<Flags>()>
class SubBuffer : public SubBufferBase {

	public:
		template<BufferUsage BuffUsage, typename Enable = typename std::enable_if<(BuffUsage & Usage) == Usage>::type>
		SubBuffer(const Buffer<BuffUsage, Flags, Transfer>& buffer, usize offset, usize len) : SubBufferBase(buffer, offset, len) {
		}

		template<BufferUsage BuffUsage>
		explicit SubBuffer(const Buffer<BuffUsage, Flags, Transfer>& buffer, usize offset = 0) : SubBuffer(buffer, offset, buffer.byte_size() - offset) {
		}
};


}

#endif // YAVE_BUFFER_SUBBUFFER_H