#!/usr/bin/env python

"""Sample script to train multi-layer perceptron on MNIST."""

from math import sqrt
import cPickle as pickle
import sys

import numpy as np
import pycuda.gpuarray as gpuarray
from sklearn.datasets import fetch_mldata

from chain import Variable, FunctionSet
from chain.functions import accuracy, Linear, relu, softmax_cross_entropy
from chain.optimizers import SGD

# Prepare dataset
print 'fetch MNIST dataset'
mnist = fetch_mldata('MNIST original', data_home='.')
mnist.data = mnist.data.astype(np.float32)
mnist.target = mnist.target.astype(np.int32)

N = 60000
x_train, x_test = np.split(mnist.data, [N])
y_train, y_test = np.split(mnist.target, [N])
N_test = y_test.size

# Initialize model
model = FunctionSet(
    l1=Linear(784,  1000, wscale=sqrt(2)),
    l2=Linear(1000, 1000, wscale=sqrt(2)),
    l3=Linear(1000,   10, wscale=sqrt(2))
)
model.to_gpu()

# Architecture
def forward(x_data, y_data):
    x = Variable(x_data)
    t = Variable(y_data)
    h1 = relu(model.l1(x))
    h2 = relu(model.l2(h1))
    y = model.l3(h2)
    return softmax_cross_entropy(y, t), accuracy(y, t)

# Setup optimizer
optimizer = SGD(lr=0.01)
optimizer.setup(model.collect_parameters())

# Learning loop
batchsize = 100
n_iter = N / batchsize
for epoch in xrange(100):
    print 'epoch', epoch

    perm = np.random.permutation(N)
    sum_accuracy = 0
    sum_loss = 0
    for i in xrange(0, N, batchsize):
        sys.stdout.write('\rtrain {} / {}'.format(i, N))

        optimizer.zero_grads()  # set gradients to zero

        # fprop/bprop
        x_batch = gpuarray.to_gpu(x_train[perm[i : i + batchsize]])
        y_batch = gpuarray.to_gpu(y_train[perm[i : i + batchsize]])
        L, acc = forward(x_batch, y_batch)
        L.backward()
        L.forget_backward()

        optimizer.update()  # update

        # stats
        sum_loss += float(L.data.get()) * batchsize
        sum_accuracy += float(acc.data.get()) * batchsize

    mean_loss = sum_loss / N
    mean_accuracy = sum_accuracy / N
    print '\ntrain mean loss=' + mean_loss + ', accuracy=' + mean_accuracy

    sum_accuracy = 0
    sum_loss = 0
    for i in xrange(0, N_test, batchsize):
        sys.stdout.write('\rtest {} / {}'.format(i, N_test))
        x_batch = gpuarray.to_gpu(x_test[i : i + batchsize])
        y_batch = gpuarray.to_gpu(y_test[i : i + batchsize])
        L, acc = forward(x_batch, y_batch)
        L.forget_backward()

        sum_loss += float(L.data.get()) * batchsize
        sum_accuracy += float(acc.data.get()) * batchsize

    mean_loss = sum_loss / N_test
    mean_accuracy = sum_accuracy / N_test
    print '\ntest mean loss=' + mean_loss + ', accuracy=' + mean_accuracy

# Save the model
with open('mlp', 'wb') as f:
    pickle.dump(model, f, -1)
