import numpy as np
import unittest
import chainer.testing as testing
import chainer.testing.condition as condition
from chainer import functions as F
from chainer import mkld

@testing.parameterize(*testing.product({
    'channel': [1, 2, 4, 8, 10, 16, 24, 32, 64]
}))
class TestRelu(unittest.TestCase):
    def setUp(self):
        self.x = np.random.rand(128, self.channel, 32, 32).astype('f')
        self.W = np.random.rand(64, self.channel, 3, 3).astype('f')
        self.b = np.random.rand(64).astype('f')
        self.y = np.random.rand(128, 64, 32, 32).astype('f')
    
    def tearDown(self):
        self.x = None
        self.y = None
        self.W = None
        self.b = None
    
    def check_relu(self):
        mkld.enable_relu = True
        f_relu = F.ReLU(False)
        res = f_relu.forward(self.x)
        gy = self.x
        res_b = f_relu.backward(self.x, gy)
        mkld.enable_relu = False
        f_relu = F.ReLU(False)
        gy = self.x
        res_expect = f_relu.forward(self.x)
        res_b_expect = f_relu.backward(self.x, gy)
        testing.assert_allclose(np.asarray(res), np.asarray(res_expect))
        testing.assert_allclose(np.asarray(res_b), np.asarray(res_b_expect))
    
    @condition.retry(3)
    def test_cpu(self):
        self.check_relu()


testing.run_module(__name__, __file__)
