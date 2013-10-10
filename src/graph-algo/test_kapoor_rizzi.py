'''
Created on Oct 9, 2013

@author: yonch
'''

from kapoor_rizzi import *
from graph_util import graph_util

if __name__ == '__main__':
    KR = kapoor_rizzi()
    
    degree = 10
    partition_n_nodes = 5
    
    g = graph_util().generate_random_regular_bipartite(partition_n_nodes, degree)
    
    KR.almost_solve(degree, g)
    