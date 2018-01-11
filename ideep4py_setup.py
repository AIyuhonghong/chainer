from setuptools.extension import Extension
from numpy import get_include
from platform import system
import sys
import external

subdir = 'mkldnn'

# Sepcify prefix under which you put ipl_mkldnn
# prefix = '/usr/local'
mkldnn_root = external.mkldnn.root()
mkldnn_version = 'aab753280e83137ba955f8f19d72cb6aaba545ef'


def prepare_mkldnn():
    external.mkldnn.prepare(mkldnn_version)


swig_opts = ['-c++', '-builtin', '-modern', '-modernargs',
             '-Iideep4py/py/mm',
             '-Iideep4py/py/primitives',
             '-Iideep4py/py/swig_utils',
             '-Iideep4py/include/primitives/',
             '-Iideep4py/include/mm/']

if sys.version_info.major < 3:
    swig_opts += ['-DNEWBUFFER_ON']

ccxx_opts = ['-std=c++11', '-Wno-unknown-pragmas']
link_opts = ['-Wl,-z,now', '-Wl,-z,noexecstack',
             '-Wl,-rpath,' + mkldnn_root + '/lib', '-L' + mkldnn_root + '/lib']

includes = [get_include(),
            'ideep4py/include',
            'ideep4py/include/mkl',
            'ideep4py/common',
            'ideep4py/include/mm',
            'ideep4py/py/mm',
            'ideep4py/py/primitives',
            'ideep4py/include/primitives',
            'ideep4py/include/blas',
            'ideep4py/include/primitives/ops',
            'ideep4py/include/primitives/prim_mgr',
            mkldnn_root + '/include']

libraries = ['mkldnn', 'mklml_intel']

if system() == 'Linux':
    ccxx_opts += ['-fopenmp', '-DOPENMP_AFFINITY']
    libraries += ['boost_system', 'glog', 'm']
    src = ['ideep4py/py/ideep4py.i',
           'ideep4py/mm/mem.cc',
           'ideep4py/mm/tensor.cc',
           'ideep4py/py/mm/mdarray.cc',
           'ideep4py/common/cpu_info.cc',
           'ideep4py/common/utils.cc',
           'ideep4py/common/common.cc',
           'ideep4py/blas/sum.cc',
           'ideep4py/py/mm/basic.cc',
           'ideep4py/primitives/ops/eltwise_fwd.cc',
           'ideep4py/primitives/ops/eltwise_bwd.cc',
           'ideep4py/primitives/eltwise.cc',
           'ideep4py/primitives/ops/conv_fwd.cc',
           'ideep4py/primitives/ops/conv_bwd_weights.cc',
           'ideep4py/primitives/ops/conv_bwd_data.cc',
           'ideep4py/primitives/ops/reorder_op.cc',
           'ideep4py/primitives/conv.cc',
           'ideep4py/primitives/ops/pooling_fwd.cc',
           'ideep4py/primitives/ops/pooling_bwd.cc',
           'ideep4py/primitives/pooling.cc',
           'ideep4py/primitives/ops/linear_fwd.cc',
           'ideep4py/primitives/ops/linear_bwd_weights.cc',
           'ideep4py/primitives/ops/linear_bwd_data.cc',
           'ideep4py/primitives/linear.cc',
           'ideep4py/primitives/bn.cc',
           'ideep4py/primitives/ops/bn_fwd.cc',
           'ideep4py/primitives/ops/bn_bwd.cc',
           'ideep4py/primitives/ops/concat_fwd.cc',
           'ideep4py/primitives/ops/concat_bwd.cc',
           'ideep4py/primitives/concat.cc',
           'ideep4py/primitives/ops/lrn_fwd.cc',
           'ideep4py/primitives/ops/lrn_bwd.cc',
           'ideep4py/primitives/lrn.cc',
           'ideep4py/primitives/dropout.cc',
           ]
else:
    # TODO
    src = ['mkldnn/mdarray.i', 'mkldnn/mdarray.cc']

ext_modules = []

ext = Extension(
    'ideep4py._ideep4py', sources=src,
    swig_opts=swig_opts,
    extra_compile_args=ccxx_opts, extra_link_args=link_opts,
    include_dirs=includes, libraries=libraries)

ext_modules.append(ext)

packages = ['ideep4py', 'ideep4py.cosim']
