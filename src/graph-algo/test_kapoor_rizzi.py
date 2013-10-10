'''
Created on Oct 9, 2013

@author: yonch
'''

from kapoor_rizzi import *
from graph_util import graph_util

if __name__ == '__main__':
    KR = kapoor_rizzi()
    
    degree = 33
    partition_n_nodes = 5
    
    # generate random graph
    g = graph_util().generate_random_regular_bipartite(partition_n_nodes, degree)
    
    # generate arbitrary partitions for approximation algo
    arbitrary = [graph_util().generate_random_regular_bipartite(partition_n_nodes, 1) for i in xrange((degree % 2) + 1)]
    
    KR.solve(degree, g, arbitrary)
    