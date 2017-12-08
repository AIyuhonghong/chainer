/*
 *COPYRIGHT
 *All modification made by Intel Corporation: © 2017 Intel Corporation.
 *Copyright (c) 2015 Preferred Infrastructure, Inc.
 *Copyright (c) 2015 Preferred Networks, Inc.
 *
 *Permission is hereby granted, free of charge, to any person obtaining a copy
 *of this software and associated documentation files (the "Software"), to deal
 *in the Software without restriction, including without limitation the rights
 *to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *copies of the Software, and to permit persons to whom the Software is
 *furnished to do so, subject to the following conditions:
 *
 *The above copyright notice and this permission notice shall be included in
 *all copies or substantial portions of the Software.
 *
 *THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *THE SOFTWARE.
 *
 *
 *######################################################################
 *# The CuPy is designed based on NumPy's API.
 *# CuPy's source code and documents contain the original NumPy ones.
 *######################################################################
 *Copyright (c) 2005-2016, NumPy Developers.
 *All rights reserved.
 *
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are
 *met:
 *
 *    * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *
 *    * Neither the name of the NumPy Developers nor the names of any
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *######################################################################
 */
#include <glog/logging.h>
#include <iostream>
#include "mkldnn.hpp"
#include "reorder_op.h"
#include "utils.h"
#include "common.h"

using namespace mkldnn;

extern engine cpu_engine;

template<typename T>
ReorderOp<T>::ReorderOp( mkldnn::memory::dims dims, mkldnn::memory::format src_fmt, mkldnn::memory::format dst_fmt)
{
    reorder_stream_.reset(new stream(stream::kind::eager));
    // create primitive
    if (reorder_prim_ == NULL) {
        setup(dims, src_fmt, dst_fmt);
    }
}

template<typename T>
ReorderOp<T>::~ReorderOp()
{
}

template<typename T>
void ReorderOp<T>::setup(mkldnn::memory::dims dims, 
                         mkldnn::memory::format src_fmt,
                         mkldnn::memory::format dst_fmt)
{
    //LOG(INFO) << "Reorder setup";
    
    assert(src_fmt != dst_mfmt);

    src_md_.reset(new memory::desc(dims, memory_data_type<T>(), src_fmt));
    dst_md_.reset(new memory::desc(dims, memory_data_type<T>(), dst_fmt));
    
    src_mem_.reset(new memory({*src_md_, cpu_engine},dummy));
    dst_mem_.reset(new memory({*dst_md_, cpu_engine},dummy));

    reorder_prim_ = std::make_shared<mkldnn::reorder>(reorder(*src_mem_, *dst_mem_));

    return;
}

template<typename T>
void ReorderOp<T>::execute(void* src, void* dst)
{
    //LOG(INFO) << "Reorder execute";
    src_mem_->set_data_handle(src);
    dst_mem_->set_data_handle(dst);
    reorder_stream_->submit({*reorder_prim_});

    //after exec, set data handle back
    src_mem_->set_data_handle(dummy);
    dst_mem_->set_data_handle(dummy);
    return;
}

template class ReorderOp<float>;


// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s
