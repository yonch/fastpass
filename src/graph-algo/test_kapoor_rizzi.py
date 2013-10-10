'''
Created on Oct 10, 2013

@author: yonch
'''
import unittest

from kapoor_rizzi import *
from graph_util import graph_util

class Test(unittest.TestCase):
    def test_approx_coloring(self):
        
        KR = kapoor_rizzi()
        
        degree = 33
        partition_n_nodes = 5
        
        # generate random graph
        g = graph_util().generate_random_regular_bipartite(partition_n_nodes, degree)
        
        # generate arbitrary partitions for approximation algo
        arbitrary = [graph_util().generate_random_regular_bipartite(partition_n_nodes, 1) for i in xrange((degree % 2) + 1)]
        
        solution = KR.solve(degree, g, arbitrary)


if __name__ == "__main__":
    #import sys;sys.argv = ['', 'Test.testName']
    unittest.main()