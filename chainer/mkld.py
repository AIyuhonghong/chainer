import numpy

import chainer
from chainer import variable


available = False

try:
    import dnn._dnn
    from dnn._dnn import mdarray
    
    available = True
except Exception as ex:
    print('*** CPU acceleration is disabled: %s' % ex)

    class mdarray(object):
        pass


def is_enabled():
    # Check whether mkldnn installed

    return available


def all_ready(inputs, check_with_ndim):
    if not is_enabled():
        return False
    _inputs = [x.data if isinstance(x, variable.Variable)
               else x for x in inputs]

    if isinstance(_inputs[0], mdarray):
        return True
    # Check whether mkldnn configured and used correctly
    elif isinstance(_inputs[0], numpy.ndarray):
        _should_use_mkldnn = True

        for x in _inputs:
            _should_use_mkldnn = _should_use_mkldnn and \
                                 x.dtype == numpy.dtype('float32')
        if _should_use_mkldnn:
            _should_use_mkldnn = _should_use_mkldnn and \
                                 chainer.should_use_mkldnn('>=auto')
        if not _should_use_mkldnn:
            return False
    # cuda.ndarray
    else:
        return False

    # Check with mkldnn supported dimension of input data
    valid_ndim = False
    for ndim in check_with_ndim:
        valid_ndim = valid_ndim or _inputs[0].ndim == ndim

    if check_with_ndim and not valid_ndim:
        return False
    return True
