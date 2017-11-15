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
#include "linear_fwd.h"
#include "utils.h"
#include "common.h"

using namespace mkldnn;

extern engine cpu_engine;

template<typename T>
LinearFwd<T>::LinearFwd(
        mkldnn::memory::dims src_d, mkldnn::memory::dims w_d,
        mkldnn::memory::dims b_d, mkldnn::memory::dims dst_d)
{
    fwd_stream_.reset(new stream(stream::kind::eager));
    //create linear primitive
    if (linear_fwd_ == NULL) {
        setup(src_d, w_d, b_d, dst_d);
    }
}

template<typename T>
LinearFwd<T>::~LinearFwd()
{
}

template<typename T>
void LinearFwd<T>::setup(mkldnn::memory::dims src_d, mkldnn::memory::dims w_d,
        mkldnn::memory::dims b_d, mkldnn::memory::dims dst_d)
{
    //LOG(INFO)<< "Linear forward setup"; 
    assert(src_d != NULL);
    assert(w_d != NULL);
    assert(b_d != NULL);//no bias case, expect as NONE_DIMS, not NULL
    assert(dst_d != NULL);
    src_md_.reset(new memory::desc({src_d}, memory_data_type<T>(),
                memory::format::any));
    weights_md_.reset(new memory::desc({w_d}, memory_data_type<T>(),
                memory::format::any));
    dst_md_.reset(new memory::desc({dst_d}, memory_data_type<T>(),
                memory::format::any));
    LOG(INFO) << "src_d" << src_d[0]<<","<< src_d[1];
    LOG(INFO) << "weight" << w_d[0] << "," << w_d[1];
    LOG(INFO) << "dst_d" << dst_d[0] << "," << dst_d[1];
    //create linear layer descriptor
    if(!b_d.empty()) {
        bias_md_.reset(new memory::desc({b_d}, memory_data_type<T>(),
                    memory::format::any));
        fwd_desc_.reset(new inner_product_forward::desc(prop_kind::forward, *src_md_,
                    *weights_md_, *bias_md_, *dst_md_));
    } else {
        fwd_desc_.reset(new inner_product_forward::desc(prop_kind::forward, *src_md_,
                    *weights_md_, *dst_md_));
    }
    //-----------Determing engine to use------------------
    //Current, treat the engine is MKLDNN::CPU
    fwd_pd_.reset(new inner_product_forward::primitive_desc(*fwd_desc_, cpu_engine));
    //create user memory primtive
    src_fmt_ = static_cast<mkldnn::memory::format>(fwd_pd_.get()->src_primitive_desc().desc().data.format);
    weights_fmt_ = static_cast<mkldnn::memory::format>(fwd_pd_.get()->weights_primitive_desc().desc().data.format);
    dst_fmt_ = static_cast<mkldnn::memory::format>(fwd_pd_.get()->dst_primitive_desc().desc().data.format);

    //create memory primitive based on dummy data
    src_mem_.reset(new memory(fwd_pd_.get()->src_primitive_desc(), dummy));
    weights_mem_.reset(new memory(fwd_pd_.get()->weights_primitive_desc(), dummy));
    dst_mem_.reset(new memory(fwd_pd_.get()->dst_primitive_desc(), dummy));
   
    /*create  linear primitive and add it to net*/
    if (!b_d.empty()) {
        bias_mem_.reset(new memory({{{b_d}, memory_data_type<T>(), memory::format::x}, cpu_engine}, dummy));
        linear_fwd_.reset(new inner_product_forward(*fwd_pd_, *src_mem_, 
                    *weights_mem_, *bias_mem_, *dst_mem_));
    } else {
        linear_fwd_.reset(new inner_product_forward(*fwd_pd_, *src_mem_,
                    *weights_mem_, *dst_mem_));
    }
    fwd_primitives_.push_back(*linear_fwd_);
    return;
}

template<typename T>
void LinearFwd<T>::execute(void* src, void* w, void* b, void* dst)
{
    //LOG(INFO) << "Linear forward";
    src_mem_->set_data_handle(src); 
    weights_mem_->set_data_handle(w);
    bias_mem_->set_data_handle(b);
    dst_mem_->set_data_handle(dst);
    //linear_fwd_->execute();
    fwd_stream_->submit(fwd_primitives_);
    //after exec, set data handle bac
    src_mem_->set_data_handle(dummy);
    weights_mem_->set_data_handle(dummy);
    bias_mem_->set_data_handle(dummy);
    dst_mem_->set_data_handle(dummy);
    return;
}

template<typename T>
void LinearFwd<T>::execute(void* src, void* w, void* dst)
{
    //LOG(INFO) << "Linear forward";
    src_mem_->set_data_handle(src); 
    weights_mem_->set_data_handle(w);
    dst_mem_->set_data_handle(dst);
    //linear_fwd_->execute();
    fwd_stream_->submit(fwd_primitives_);
    //after exec, set data handle bac
    src_mem_->set_data_handle(dummy);
    weights_mem_->set_data_handle(dummy);
    dst_mem_->set_data_handle(dummy);
    return;
}
template class LinearFwd<float>;


