import mkldnn.api.memory as m
import mkldnn.api.cosim_dump as cdump

from mkldnn.chainer import cosim, is_cosim
from mkldnn.chainer.pooling_2d import Pooling2DMKLDNN, Pooling2DForward, Pooling2DBackward
from mkldnn.api.support import pooling_max
from mkldnn.chainer.runtime import Engine
from mkldnn.compute_complex import array
from mkldnn.api.cosim_dump import *


class MaxPooling2DMKLDNN(Pooling2DMKLDNN):

    """Max pooling over a set of 2d planes."""

    def __init__(self, ksize, stride=None, pad=0, cover_all=True):
        super(MaxPooling2DMKLDNN, self).__init__(ksize, stride, pad, cover_all)

        if is_cosim():
            from chainer.functions.pooling.max_pooling_2d import MaxPooling2D
            self.cosim_func = MaxPooling2D(ksize, stride, pad, cover_all)

    def forward_cpu(self, x):
        cc = Pooling2DForward(x, pooling_max, ksize=(self.kh, self.kw),
                              stride=(self.sy, self.sx),
                              pad=(self.ph, self.pw), cover_all=self.cover_all,
                              pos=(self.rank, self.fanout))

        self.hint = cc.hint
        self.ws = cc.ws
        y, = cc.execute_on()
        self.y = y

        cosim.cosim_verify(self, (y, ), x)
        return y,

    def backward_cpu(self, x, gy):
        cc = Pooling2DBackward(x, gy[0], self.hint, self.y, self.ws, pooling_max,
                               ksize=(self.kh, self.kw),
                               stride=(self.sy, self.sx),
                               pad=(self.ph, self.pw), cover_all=self.cover_all,
                               pos=(self.rank, self.fanout))
        gx, = cc.execute_on()

        cosim.cosim_verify(self, (gx, ), x, gy)
        return gx,

    def cpu_cosim_dump_inner(self, in_data, out_grad=None):
        cd = None
        if out_grad is None:
            cd = cdump.cosim_dump(cdump_op_max_pooling_forward)
        else:
            cd = cdump.cosim_dump(cdump_op_max_pooling_backward)

        x = array(in_data[0], m.memory.nchw, Engine())
        cd.dump_memory(cdump_src_memory, x.memory)

        if out_grad is not None:
            gy = array(out_grad[0], m.memory.nchw, Engine())
            cd.dump_memory(cdump_diff_dst_memory, gy.memory)

        cd.dump_int_parms(cdump_max_pooling_int_parms, 8,
                          self.kh, self.kw, self.sy, self.sx, self.ph, self.pw,
                          pooling_max, 1 if self.cover_all else 0)

