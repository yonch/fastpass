'''
Created on October 28, 2013

@author: aousterh
'''
import sys
import unittest

sys.path.insert(0, '../../src/graph-algo')
sys.path.insert(0, '../../bindings/graph-algo')

from graph_util import graph_util
from kr_util import kr_util
import graph
import kapoorrizzi

class timing_util(object):
    '''
    Functions for timing kr code.
    '''

    def __init__(self, degree, n_nodes):
        # initialize kr
        generator = kr_util()
        self.kr = generator.build_kr(n_nodes, degree)

        g_p = graph_util().generate_random_regular_bipartite(n_nodes, degree)
        arbitrary_p = graph_util().generate_random_regular_bipartite(n_nodes, 1)
        
        # create the C versions of the graphs
        self.g_c = graph.create_graph_test(n_nodes)
        for edge in g_p.edges_iter():
            graph.add_edge(self.g_c, edge[0], edge[1])
        self.arbitrary_c = graph.create_graph_test(n_nodes)
        for edge in arbitrary_p.edges_iter():
            graph.add_edge(self.arbitrary_c, edge[0], edge[1])

        # allocate solution
        self.solution = kapoorrizzi.create_matching_set()

    def solve_c(self):
        kapoorrizzi.solve(self.kr, self.g_c, self.arbitrary_c, self.solution)

        
    
