import numpy
from chainer import testing
from chainer import utils
import dnn._dnn

x1 = numpy.ndarray(shape=(2,2,2,2), dtype=numpy.float32, order='C')
x = dnn._dnn.mdarray(x1)
x = x + 1
testing.assert_allclose(x1+1, x)

x = dnn._dnn.mdarray(x1)

print(x)
print("ndims=", x.ndim)
print("shape=", x.shape)
print("size=", x.size)
print("dtype=", x.dtype)
print("is_mdarry=", x.is_mdarray)

x1 += x
x += x
x2 = numpy.array(x)
testing.assert_allclose(x1, x2)


x1 = numpy.ones(shape=(2,2,2,2), dtype=numpy.float32, order='C')
x = dnn._dnn.mdarray(x1)
y = x + x1
y2 = numpy.array(y)
testing.assert_allclose(y2, x1+x1)

y = x * x1
y2 = numpy.array(y)
testing.assert_allclose(y2, x1*x1)

x1 = numpy.random.uniform(-1, 1, (3, 4)).astype(numpy.float32)
x = dnn._dnn.mdarray(x1)
z1 = (x1 > 0).astype(x1.dtype)
z = (x > 0).astype(x1.dtype)
testing.assert_allclose(z, z1)
