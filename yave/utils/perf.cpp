/*******************************
Copyright (c) 2016-2021 Grégoire Angerand

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

#include "perf.h"

#ifdef YAVE_PERF_LOG_ENABLED
#include <atomic>
#endif


namespace yave {
namespace perf {

namespace {
static struct Guard {
    ~Guard() {
        if(is_capturing()) {
            y_fatal("Capture still in progress.");
        }
    }

    void use() {
    }
} guard;
}

#ifdef YAVE_PERF_LOG_ENABLED

static std::atomic<bool> tracy_recording = false;

void start_capture(const char*) {
    tracy_recording = true;
}

void end_capture() {
    tracy_recording = false;
}

bool is_capturing() {
    return tracy_recording;
}

#endif

}
}

