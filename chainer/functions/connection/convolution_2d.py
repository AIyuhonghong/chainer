import numpy

import chainer
from chainer import configuration
from chainer import cuda
from chainer import function_node
import chainer.functions
from chainer.utils import argument
from chainer.utils import conv
from chainer.utils import type_check
from chainer import ideepy

import dnn._dnn
from dnn._dnn import mdarray, conv_param_t, Convolution2D_Py_F32

if cuda.cudnn_enabled:
    cudnn = cuda.cudnn
    libcudnn = cuda.cudnn.cudnn
    _cudnn_version = libcudnn.getVersion()
    _fwd_pref = libcudnn.CUDNN_CONVOLUTION_FWD_SPECIFY_WORKSPACE_LIMIT
    _bwd_filter_pref = \
        libcudnn.CUDNN_CONVOLUTION_BWD_FILTER_SPECIFY_WORKSPACE_LIMIT
    _bwd_data_pref = \
        libcudnn.CUDNN_CONVOLUTION_BWD_DATA_SPECIFY_WORKSPACE_LIMIT


def _pair(x):
    if hasattr(x, '__getitem__'):
        return x
    return x, x


class Convolution2DFunction(function_node.FunctionNode):

    def __init__(self, stride=1, pad=0, cover_all=False, **kwargs):
        argument.check_unexpected_kwargs(
            kwargs,
            deterministic="deterministic argument is not supported anymore. "
            "Use chainer.using_config('cudnn_deterministic', value) context "
            "where value is either `True` or `False`.",
            requires_x_grad="requires_x_grad argument is not supported "
            "anymore. Just remove the argument. Note that whether to compute "
            "the gradient w.r.t. x is automatically decided during "
            "backpropagation."
        )
        dilate, = argument.parse_kwargs(kwargs, ('dilate', 1))

        self.sy, self.sx = _pair(stride)
        self.ph, self.pw = _pair(pad)
        self.cover_all = cover_all
        self.dy, self.dx = _pair(dilate)

    def check_type_forward(self, in_types):
        n_in = in_types.size()
        type_check.expect(2 <= n_in, n_in <= 3)

        x_type = in_types[0]
        w_type = in_types[1]
        type_check.expect(
            x_type.dtype.kind == 'f',
            w_type.dtype.kind == 'f',
            x_type.ndim == 4,
            w_type.ndim == 4,
            x_type.shape[1] == w_type.shape[1],
        )

        if type_check.eval(n_in) == 3:
            b_type = in_types[2]
            type_check.expect(
                b_type.dtype == x_type.dtype,
                b_type.ndim == 1,
                b_type.shape[0] == w_type.shape[0],
            )
    def forward_ia(self, inputs):
        # FIXME: only support dilate == 1 currently
        if self.dy != 1 or self.dx != 1:
            return self.forward_cpu(self, inputs)

        self.retain_inputs((0, 1))  # retain only x and W
        x, W = inputs[:2]
        b = inputs[2] if len(inputs) == 3 else None
        out_c, input_c, kh, kw = W.shape
        n, c, h, w = x.shape

        out_h = conv.get_conv_outsize(h, kh, self.sy, self.ph, cover_all=self.cover_all)
        assert out_h > 0, 'Height in the output should be positive.'
        out_w = conv.get_conv_outsize(w, kw, self.sx, self.pw, cover_all=self.cover_all)
        assert out_w > 0, 'Width in the output should be positive.'
        self.pd = self.sy*(out_h-1) + kh - h - self.ph
        self.pr = self.sx*(out_w-1) + kw - w - self.pw

        # create conv parameter
        # for IA specific
        self.cp = conv_param_t()
        self.cp.src_d1, self.cp.src_d2, self.cp.src_d3, self.cp.src_d4 = x.shape
        self.cp.weights_d1, self.cp.weights_d2, self.cp.weights_d3, self.cp.weights_d4 = W.shape
        self.cp.dst_d1, self.cp.dst_d2, self.cp.dst_d3, self.cp.dst_d4 = n, out_c, out_h, out_w
        self.cp.bias_d1 = inputs[2].shape[0] if len(inputs) ==  3 else -1
        self.cp.with_bias = True if len(inputs) == 3 else False
        self.cp.sy, self.cp.sx = self.sy, self.sx
        self.cp.pad_lh, self.cp.pad_lw, self.cp.pad_rh, self.cp.pad_rw = self.ph, self.pw, self.pd, self.pr

        if isinstance(x, numpy.ndarray):
            if x.flags.contiguous is False:
                x = numpy.ascontiguousarray(x)
            x = mdarray(x)
        if isinstance(W, numpy.ndarray):
            if W.flags.contiguous is False:
                W = numpy.ascontiguousarray(W)
            W = mdarray(W)
        if self.cp.with_bias and isinstance(b, numpy.ndarray):
            if b.flags.contiguous is False:
                b = numpy.ascontiguousarray(b)
            b = mdarray(b)
	
        if self.cp.with_bias:
            y = Convolution2D_Py_F32.Forward(x, W, b, self.cp)
        else:
            y = Convolution2D_Py_F32.Forward(x, W, None, self.cp)

        return y,

    def forward_cpu(self, inputs):
        self.retain_inputs((0, 1))  # retain only x and W
        x, W = inputs[:2]
        b = inputs[2] if len(inputs) == 3 else None

        if not all([isinstance(i, numpy.ndarray) for i in inputs]):
            if b is not None:
                raise ValueError('numpy and cupy must not be used together\n'
                                 'type(W): {0}, type(x): {1}, type(b): {2}'
                                 .format(type(W), type(x), type(b)))
            else:
                raise ValueError('numpy and cupy must not be used together\n'
                                 'type(W): {0}, type(x): {1}'
                                 .format(type(W), type(x)))

        kh, kw = W.shape[2:]
        col = conv.im2col_cpu(
            x, kh, kw, self.sy, self.sx, self.ph, self.pw,
            cover_all=self.cover_all, dy=self.dy, dx=self.dx)
        y = numpy.tensordot(
            col, W, ((1, 2, 3), (1, 2, 3))).astype(x.dtype, copy=False)
        if b is not None:
            y += b
        return numpy.rollaxis(y, 3, 1),

    def forward_gpu(self, inputs):
        self.retain_inputs((0, 1))  # retain only x and W
        x, W = inputs[:2]
        b = inputs[2] if len(inputs) == 3 else None

        if not all([isinstance(i, cuda.ndarray) for i in inputs]):
            if b is not None:
                raise ValueError('numpy and cupy must not be used together\n'
                                 'type(W): {0}, type(x): {1}, type(b): {2}'
                                 .format(type(W), type(x), type(b)))
            else:
                raise ValueError('numpy and cupy must not be used together\n'
                                 'type(W): {0}, type(x): {1}'
                                 .format(type(W), type(x)))

        out_c, _, kh, kw = W.shape
        n, c, h, w = x.shape

        out_h = conv.get_conv_outsize(h, kh, self.sy, self.ph,
                                      cover_all=self.cover_all, d=self.dy)
        assert out_h > 0, 'Height in the output should be positive.'
        out_w = conv.get_conv_outsize(w, kw, self.sx, self.pw,
                                      cover_all=self.cover_all, d=self.dx)
        assert out_w > 0, 'Width in the output should be positive.'

        y = cuda.cupy.empty((n, out_c, out_h, out_w), dtype=x.dtype)
        if (not self.cover_all and chainer.should_use_cudnn('>=auto') and
                x.dtype == W.dtype and
                ((self.dy == 1 and self.dx == 1) or _cudnn_version >= 6000)):
            x = cuda.cupy.ascontiguousarray(x)
            W = cuda.cupy.ascontiguousarray(W)
            if b is not None:
                b = cuda.cupy.ascontiguousarray(b)

            use_tensor_core = chainer.should_use_cudnn_tensor_core(x.dtype)

            handle = cudnn.get_handle()
            x_desc = cudnn.create_tensor_descriptor(x)
            y_desc = cudnn.create_tensor_descriptor(y)

            filter_desc = cudnn.create_filter_descriptor(W)
            conv_desc = cudnn.create_convolution_descriptor(
                (self.ph, self.pw), (self.sy, self.sx), x.dtype,
                dilation=(self.dy, self.dx),
                use_tensor_core=use_tensor_core)
            if b is not None:
                bias_desc = cudnn.create_tensor_descriptor(
                    b[None, :, None, None])

            workspace_size = cuda.get_max_workspace_size()
            workspace = cuda.cupy.empty((workspace_size,), dtype='b')
            algo = libcudnn.getConvolutionForwardAlgorithm(
                handle, x_desc.value, filter_desc.value,
                conv_desc.value, y_desc.value, _fwd_pref, workspace_size)

            if use_tensor_core:
                # Only CUDNN_CONVOLUTION_FWD_ALGO_IMPLICIT_PRECOMP_GEMM
                # supports Tensor-Core in cuDNN7.
                algo = libcudnn.CUDNN_CONVOLUTION_FWD_ALGO_IMPLICIT_PRECOMP_GEMM  # NOQA

            oz_dtype = 'd' if x.dtype == 'd' else 'f'
            one = numpy.array(1, dtype=oz_dtype).ctypes
            zero = numpy.array(0, dtype=oz_dtype).ctypes
            libcudnn.convolutionForward(
                handle, one.data, x_desc.value, x.data.ptr,
                filter_desc.value, W.data.ptr, conv_desc.value,
                algo, workspace.data.ptr, workspace_size, zero.data,
                y_desc.value, y.data.ptr)

            # TODO(beam2d): Support unshared bias
            if b is not None:
                cudnn.add_tensor(
                    handle, one.data, bias_desc.value, b.data.ptr,
                    one.data, y_desc.value, y.data.ptr)
        else:
            # Implementation using im2col
            col = conv.im2col_gpu(
                x, kh, kw, self.sy, self.sx, self.ph, self.pw,
                cover_all=self.cover_all, dy=self.dy, dx=self.dx)
            y = cuda.cupy.tensordot(
                col, W, ((1, 2, 3), (1, 2, 3))).astype(x.dtype, copy=False)
            # TODO(beam2d): Support unshared bias
            if b is not None:
                y += b
            y = cuda.cupy.rollaxis(y, 3, 1)

        return y,
    
    def backward(self, indexes, grad_outputs):
        x, W = self.get_retained_inputs()
        gy, = grad_outputs

        ret = []
        if 0 in indexes:
            xh, xw = x.shape[2:]
            gx = chainer.functions.deconvolution_2d(
                gy, W, stride=(self.sy, self.sx), pad=(self.ph, self.pw),
                outsize=(xh, xw), dilate=(self.dy, self.dx))
            ret.append(gx)
        if 1 in indexes:
            gW, = Convolution2DGradW(self).apply((x, gy))
            ret.append(gW)
        if 2 in indexes:
            gb = chainer.functions.sum(gy, axis=(0, 2, 3))
            ret.append(gb)

        return ret


class Convolution2DGradW(function_node.FunctionNode):

    def __init__(self, conv2d):
        W_node = conv2d.inputs[1]
        self.kh, self.kw = W_node.shape[2:]
        self.sy = conv2d.sy
        self.sx = conv2d.sx
        self.ph = conv2d.ph
        self.pw = conv2d.pw
        self.dy = conv2d.dy
        self.dx = conv2d.dx
        self.cover_all = conv2d.cover_all
        self.W_dtype = W_node.dtype

    def forward_ia(self, inputs):
        # FIXME: only support dilate == 1 currently
        if self.dy != 1 or self.dx != 1:
            return self.forward_cpu(self, inputs)
        
        self.retain_inputs((0, 1))
        x, gy = inputs

        n, input_c, h, w = x.shape
        n, out_c, out_h, out_w = gy.shape
        self.pd = self.sy*(out_h-1) + self.kh - h - self.ph
        self.pr = self.sx*(out_w-1) + self.kw - w - self.pw

        # create conv parameter
        # for IA specific
        self.cp = conv_param_t()
        self.cp.src_d1, self.cp.src_d2, self.cp.src_d3, self.cp.src_d4 = x.shape
        self.cp.weights_d1, self.cp.weights_d2, self.cp.weights_d3, self.cp.weights_d4 = out_c, input_c, self.kh, self.kw
        self.cp.dst_d1, self.cp.dst_d2, self.cp.dst_d3, self.cp.dst_d4 = gy.shape
        self.cp.sy, self.cp.sx = self.sy, self.sx
        self.cp.pad_lh, self.cp.pad_lw, self.cp.pad_rh, self.cp.pad_rw = self.ph, self.pw, self.pd, self.pr
        # Chainer's this function is only to calculate gW, MUST no gb
        self.cp.bias_d1 = -1
        self.cp.with_bias = False

        if isinstance(x, numpy.ndarray):
            if x.flags.contiguous is False:
                x = numpy.ascontiguousarray(x)
            x = mdarray(x)
        if isinstance(gy, numpy.ndarray):
            if gy.flags.contiguous is False:
                gy = numpy.ascontiguousarray(gy)
            gy = mdarray(gy)

        # only calculate gW, no gb
        (gW,) = Convolution2D_Py_F32.BackwardWeights(x, gy, self.cp)
        return gW,

    def forward_cpu(self, inputs):
        self.retain_inputs((0, 1))
        x, gy = inputs
        col = conv.im2col_cpu(
            x, self.kh, self.kw, self.sy, self.sx, self.ph, self.pw,
            cover_all=self.cover_all, dy=self.dy, dx=self.dx)

        # NumPy raises an error when the array is not contiguous.
        # See: https://github.com/chainer/chainer/issues/2744
        # TODO(niboshi): Remove this code when NumPy is fixed.
        if (not (gy.flags.c_contiguous or gy.flags.f_contiguous) and
                1 in gy.shape):
            gy = numpy.ascontiguousarray(gy)

        gW = numpy.tensordot(
            gy, col, ((0, 2, 3), (0, 4, 5))).astype(self.W_dtype, copy=False)
        return gW,

    def forward_gpu(self, inputs):
        self.retain_inputs((0, 1))
        x, gy = inputs
        _, out_c, out_h, out_w = gy.shape
        n, c, h, w = x.shape

        if (self.cover_all or not chainer.should_use_cudnn('>=auto') or
                x.dtype != self.W_dtype or
                ((self.dy > 1 or self.dx > 1) and _cudnn_version < 6000)):
            col = conv.im2col_gpu(
                x, self.kh, self.kw, self.sy, self.sx, self.ph, self.pw,
                cover_all=self.cover_all, dy=self.dy, dx=self.dx)
            gW = cuda.cupy.tensordot(
                gy, col, ((0, 2, 3), (0, 4, 5))).astype(self.W_dtype,
                                                        copy=False)
            return gW,

        gW = cuda.cupy.empty((out_c, c, self.kh, self.kw), dtype=self.W_dtype)
        x = cuda.cupy.ascontiguousarray(x)
        gy = cuda.cupy.ascontiguousarray(gy)

        use_tensor_core = chainer.should_use_cudnn_tensor_core(x.dtype)

        handle = cudnn.get_handle()
        x_desc = cudnn.create_tensor_descriptor(x)
        gy_desc = cudnn.create_tensor_descriptor(gy)

        filter_desc = cudnn.create_filter_descriptor(gW)
        conv_desc = cudnn.create_convolution_descriptor(
            (self.ph, self.pw), (self.sy, self.sx), x.dtype,
            dilation=(self.dy, self.dx),
            use_tensor_core=use_tensor_core)

        oz_dtype = 'd' if x.dtype == 'd' else 'f'
        one = numpy.array(1, dtype=oz_dtype).ctypes
        zero = numpy.array(0, dtype=oz_dtype).ctypes

        workspace_size = cuda.get_max_workspace_size()
        workspace = cuda.cupy.empty((workspace_size,), dtype='b')

        if configuration.config.cudnn_deterministic:
            algo = libcudnn.CUDNN_CONVOLUTION_BWD_FILTER_ALGO_1
        else:
            algo = libcudnn.getConvolutionBackwardFilterAlgorithm(
                handle, x_desc.value, gy_desc.value, conv_desc.value,
                filter_desc.value, _bwd_filter_pref, workspace_size)

        if use_tensor_core:
            # Only CUDNN_CONVOLUTION_BWD_FILTER_ALGO_1 supports
            # Tensor-Core in cuDNN7.
            algo = libcudnn.CUDNN_CONVOLUTION_BWD_FILTER_ALGO_1

        libcudnn.convolutionBackwardFilter_v3(
            handle, one.data, x_desc.value, x.data.ptr, gy_desc.value,
            gy.data.ptr, conv_desc.value, algo, workspace.data.ptr,
            workspace_size, zero.data, filter_desc.value, gW.data.ptr)

        return gW,

    def backward(self, indexes, grad_outputs):
        x, gy = self.get_retained_inputs()
        ggW, = grad_outputs

        ret = []
        if 0 in indexes:
            xh, xw = x.shape[2:]
            gx = chainer.functions.deconvolution_2d(
                gy, ggW, stride=(self.sy, self.sx), pad=(self.ph, self.pw),
                outsize=(xh, xw), dilate=(self.dy, self.dx))
            ret.append(gx)
        if 1 in indexes:
            ggy = convolution_2d(
                x, ggW, stride=(self.sy, self.sx), pad=(self.ph, self.pw),
                cover_all=self.cover_all, dilate=(self.dy, self.dx))
            ret.append(ggy)

        return ret


def convolution_2d(x, W, b=None, stride=1, pad=0, cover_all=False, **kwargs):
    """convolution_2d(x, W, b=None, stride=1, pad=0, cover_all=False)

    Two-dimensional convolution function.

    This is an implementation of two-dimensional convolution in ConvNets.
    It takes three variables: the input image ``x``, the filter weight ``W``,
    and the bias vector ``b``.

    Notation: here is a notation for dimensionalities.

    - :math:`n` is the batch size.
    - :math:`c_I` and :math:`c_O` are the number of the input and output
      channels, respectively.
    - :math:`h_I` and :math:`w_I` are the height and width of the input image,
      respectively.
    - :math:`h_K` and :math:`w_K` are the height and width of the filters,
      respectively.
    - :math:`h_P` and :math:`w_P` are the height and width of the spatial
      padding size, respectively.

    Then the ``Convolution2D`` function computes correlations between filters
    and patches of size :math:`(h_K, w_K)` in ``x``.
    Note that correlation here is equivalent to the inner product between
    expanded vectors.
    Patches are extracted at positions shifted by multiples of ``stride`` from
    the first position ``(-h_P, -w_P)`` for each spatial axis.
    The right-most (or bottom-most) patches do not run over the padded spatial
    size.

    Let :math:`(s_Y, s_X)` be the stride of filter application. Then, the
    output size :math:`(h_O, w_O)` is determined by the following equations:

    .. math::

       h_O &= (h_I + 2h_P - h_K) / s_Y + 1,\\\\
       w_O &= (w_I + 2w_P - w_K) / s_X + 1.

    If ``cover_all`` option is ``True``, the filter will cover the all
    spatial locations. So, if the last stride of filter does not cover the
    end of spatial locations, an addtional stride will be applied to the end
    part of spatial locations. In this case, the output size :math:`(h_O, w_O)`
    is determined by the following equations:

    .. math::

       h_O &= (h_I + 2h_P - h_K + s_Y - 1) / s_Y + 1,\\\\
       w_O &= (w_I + 2w_P - w_K + s_X - 1) / s_X + 1.

    If the bias vector is given, then it is added to all spatial locations of
    the output of convolution.

    The output of this function can be non-deterministic when it uses cuDNN.
    If ``chainer.configuration.config.cudnn_deterministic`` is ``True`` and
    cuDNN version is >= v3, it forces cuDNN to use a deterministic algorithm.

    When the dilation factor is greater than one, cuDNN is not used unless
    the version is 6.0 or higher.

    .. warning::

        ``deterministic`` argument is not supported anymore since v2.
        Instead, use ``chainer.using_config('cudnn_deterministic', value)``
        (value is either ``True`` or ``False``).
        See :func:`chainer.using_config`.

    Args:
        x (:class:`~chainer.Variable` or :class:`numpy.ndarray` or \
        :class:`cupy.ndarray`):
            Input variable of shape :math:`(n, c_I, h_I, w_I)`.
        W (:class:`~chainer.Variable` or :class:`numpy.ndarray` or \
        :class:`cupy.ndarray`):
            Weight variable of shape :math:`(c_O, c_I, h_K, w_K)`.
        b (:class:`~chainer.Variable` or :class:`numpy.ndarray` or \
        :class:`cupy.ndarray`): Bias variable of length :math:`c_O` (optional).
        stride (:class:`int` or pair of :class:`int` s):
            Stride of filter applications. ``stride=s`` and ``stride=(s, s)``
            are equivalent.
        pad (:class:`int` or pair of :class:`int` s):
            Spatial padding width for input arrays.
            ``pad=p`` and ``pad=(p, p)`` are equivalent.
        cover_all (bool): If ``True``, all spatial locations are convoluted
            into some output pixels.
        dilate (int or pair of ints): Dilation factor of filter applications.
            ``dilate=d`` and ``dilate=(d, d)`` are equivalent.

    Returns:
        ~chainer.Variable:
            Output variable of shape :math:`(n, c_O, h_O, w_O)`.

    .. seealso:: :class:`~chainer.links.Convolution2D`

    .. admonition:: Example

        >>> n = 10
        >>> c_i, c_o = 3, 1
        >>> h_i, w_i = 30, 40
        >>> h_k, w_k = 10, 10
        >>> h_p, w_p = 5, 5
        >>> x = np.random.uniform(0, 1, (n, c_i, h_i, w_i)).astype('f')
        >>> x.shape
        (10, 3, 30, 40)
        >>> W = np.random.uniform(0, 1, (c_o, c_i, h_k, w_k)).astype('f')
        >>> W.shape
        (1, 3, 10, 10)
        >>> b = np.random.uniform(0, 1, (c_o,)).astype('f')
        >>> b.shape
        (1,)
        >>> s_y, s_x = 5, 7
        >>> y = F.convolution_2d(x, W, b, stride=(s_y, s_x), pad=(h_p, w_p))
        >>> y.shape
        (10, 1, 7, 6)
        >>> h_o = int((h_i + 2 * h_p - h_k) / s_y + 1)
        >>> w_o = int((w_i + 2 * w_p - w_k) / s_x + 1)
        >>> y.shape == (n, c_o, h_o, w_o)
        True
        >>> y = F.convolution_2d(x, W, b, stride=(s_y, s_x), pad=(h_p, w_p), \
cover_all=True)
        >>> y.shape == (n, c_o, h_o, w_o + 1)
        True

    """
    argument.check_unexpected_kwargs(
        kwargs, deterministic="deterministic argument is not "
        "supported anymore. "
        "Use chainer.using_config('cudnn_deterministic', value) "
        "context where value is either `True` or `False`.")
    dilate, = argument.parse_kwargs(kwargs, ('dilate', 1))

    fnode = Convolution2DFunction(stride, pad, cover_all, dilate=dilate)
    if b is None:
        args = x, W
    else:
        args = x, W, b
    y, = fnode.apply(args)
    return y
