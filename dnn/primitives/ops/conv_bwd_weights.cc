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
#include "conv_bwd_weights.h"
#include "utils.h"
#include "common.h"

using namespace mkldnn;

extern engine cpu_engine;

template<typename T>
Convolution2DBwdWeights<T>::Convolution2DBwdWeights( 
        mkldnn::memory::dims src_d, mkldnn::memory::dims diff_w_d,
        mkldnn::memory::dims diff_b_d, mkldnn::memory::dims diff_dst_d,
        int sy, int sx,
        int pad_lh, int pad_lw, int pad_rh, int pad_rw)
{
    bwd_weights_stream_.reset(new stream(stream::kind::eager));
    // create conv primitive
    if (conv_bwd_weights_ == NULL) {
        setup(src_d, diff_w_d, diff_b_d, diff_dst_d,
                sy, sx,
                pad_lh, pad_lw,
                pad_rh, pad_rw);
    }
}

template<typename T>
Convolution2DBwdWeights<T>::~Convolution2DBwdWeights()
{
}

template<typename T>
void Convolution2DBwdWeights<T>::setup(mkldnn::memory::dims src_d, mkldnn::memory::dims diff_w_d,
        mkldnn::memory::dims diff_b_d, mkldnn::memory::dims diff_dst_d,
        int s1, int s2,
        int pl1, int pl2,
        int pr1, int pr2)
{
    //LOG(INFO) << "Convolution backward_setup";
    assert(src_d != NULL);
    assert(diff_w_d != NULL);
    assert(diff_b_d != NULL); // no bias case, expect as NONE_DIMS, not NULL
    assert(diff_dst_d != NULL);

    strides_ = {s1, s2};
    padding_l_ = {pl1, pl2};
    padding_r_ = {pr1, pr2};

    /* create memory descriptors for convolution data w/ no specified format */
    src_md_.reset(new memory::desc({src_d}, memory_data_type<T>(),
                                   memory::format::any));
    diff_weights_md_.reset(new memory::desc({diff_w_d},
                                       memory_data_type<T>(), memory::format::any));
    diff_dst_md_.reset(new memory::desc({diff_dst_d}, memory_data_type<T>(),
                                   memory::format::any));
    if (!diff_b_d.empty())
        diff_bias_md_.reset(new memory::desc({diff_b_d}, memory_data_type<T>(),
                                   memory::format::any));
    /* create a convolution */
    if (!diff_b_d.empty()) {
        bwd_weights_desc_.reset(new convolution_backward_weights::desc(
                    convolution_direct, *src_md_, *diff_weights_md_,
                    *diff_bias_md_, *diff_dst_md_, strides_, padding_l_, padding_r_, padding_kind::zero));
    } else {
        bwd_weights_desc_.reset(new convolution_backward_weights::desc(
                    convolution_direct, *src_md_, *diff_weights_md_,
                    *diff_dst_md_, strides_, padding_l_, padding_r_, padding_kind::zero));

    }

    // FIXME
    // yli135: Current conv bwd need a fwd pd as hint, will remove in future
    fwd_desc_.reset(new convolution_forward::desc(prop_kind::forward,
                convolution_direct, *src_md_, *diff_weights_md_,
                *diff_dst_md_, strides_, padding_l_, padding_r_, padding_kind::zero));
    fwd_pd_.reset(new convolution_forward::primitive_desc(*fwd_desc_, cpu_engine));

    /* create backward conv prim desc*/
    bwd_weights_pd_.reset(new convolution_backward_weights::primitive_desc(
                *bwd_weights_desc_, cpu_engine, *fwd_pd_));


    //store the expected memory format
    src_fmt_ = static_cast<mkldnn::memory::format>(bwd_weights_pd_.get()->src_primitive_desc().desc().data.format);
    diff_weights_fmt_ = static_cast<mkldnn::memory::format>(bwd_weights_pd_.get()->diff_weights_primitive_desc().desc().data.format);
    diff_dst_fmt_ = static_cast<mkldnn::memory::format>(bwd_weights_pd_.get()->diff_dst_primitive_desc().desc().data.format);
    
    // create memory primitive based on dummy data
    src_mem_.reset(new memory(bwd_weights_pd_.get()->src_primitive_desc(), dummy));
    diff_weights_mem_.reset(new memory(bwd_weights_pd_.get()->diff_weights_primitive_desc(), dummy));
    diff_dst_mem_.reset(new memory(bwd_weights_pd_.get()->diff_dst_primitive_desc(), dummy));

    /* create convolution primitive and add it to net */
    if (!diff_b_d.empty()) {
        diff_bias_mem_.reset(new memory({{{diff_b_d}, memory_data_type<T>(), memory::format::x}, cpu_engine}, dummy));
        conv_bwd_weights_.reset(new convolution_backward_weights(*bwd_weights_pd_, *src_mem_,
                                      *diff_dst_mem_, *diff_weights_mem_, *diff_bias_mem_));
    } else {
        conv_bwd_weights_.reset(new convolution_backward_weights(*bwd_weights_pd_, *src_mem_,
                                      *diff_dst_mem_, *diff_weights_mem_));
    }

    bwd_weights_primitives_.push_back(*conv_bwd_weights_);
    return;
}

template<typename T>
void Convolution2DBwdWeights<T>::execute(mkldnn::memory src, mkldnn::memory diff_w, mkldnn::memory diff_b, mkldnn::memory diff_dst)
{
//    LOG(INFO) << "Convolution forward";
    //LOG(INFO) << "conv_fwd_:" << conv_fwd_;
    //LOG(INFO) << "x=" << x << "; x_size=" << x_d1*x_d2*x_d3*x_d4*4;
    src_mem_->set_data_handle(src.get_data_handle());
    diff_weights_mem_->set_data_handle(diff_w.get_data_handle());
    diff_bias_mem_->set_data_handle(diff_b.get_data_handle());
    diff_dst_mem_->set_data_handle(diff_dst.get_data_handle());
    //conv_fwd_->execute();
    bwd_weights_stream_->submit(bwd_weights_primitives_);
    return;
}

template<typename T>
void Convolution2DBwdWeights<T>::execute(mkldnn::memory src, mkldnn::memory diff_w, mkldnn::memory diff_dst)
{
//    LOG(INFO) << "Convolution forward without bias";
//    LOG(INFO) << conv_fwd_;

    src_mem_->set_data_handle(src.get_data_handle());
    diff_weights_mem_->set_data_handle(diff_w.get_data_handle());
    diff_dst_mem_->set_data_handle(diff_dst.get_data_handle());
    //conv_fwd_->execute();
    bwd_weights_stream_->submit(bwd_weights_primitives_);
    return;
}

template class Convolution2DBwdWeights<float>;


// vim: et ts=4 sw=4 cindent cino^=l0,\:0,N-s
