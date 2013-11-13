'''
Created on October 28, 2013

@author: aousterh
'''
import sys
import unittest

sys.path.insert(0, '../../src/graph-algo')
sys.path.insert(0, '../../bindings/graph-algo')

from graph_util import graph_util
from kapoor_rizzi import *
from kr_util import kr_util
import graph
import kapoorrizzi

class c_timing_util(object):
    '''
    Functions for timing kr code C implementation
    '''

    def __init__(self, degree, n_nodes):
        # initialize kr
        generator = kr_util()
        self.kr = generator.build_kr(n_nodes, degree)

        g_p = graph_util().generate_random_regular_bipartite(n_nodes, degree)
        arbitrary_p = graph_util().generate_random_regular_bipartite(n_nodes, 1)
        
        # create the C versions of the graphs
        self.g_c_structure = graph.create_graph_structure_test(n_nodes)
        self.g_c = graph.create_graph_edges_test(n_nodes)
        self.g_c_copy = graph.create_graph_edges_test(n_nodes)
        for edge in g_p.edges_iter():
            graph.add_edge(self.g_c_structure, self.g_c, edge[0], edge[1])
        graph.copy_edges(self.g_c, self.g_c_copy, n_nodes)
 
        self.arbitrary_c = graph.create_graph_edges_test(n_nodes)
        for edge in arbitrary_p.edges_iter():
            graph.add_edge(self.g_c_structure, self.g_c, edge[0], edge[1])
            graph.set_edge(self.g_c_structure, self.g_c_copy, self.arbitrary_c, edge[0], edge[1])

        # allocate solution
        self.solution = kapoorrizzi.create_matching_set()

    def solve(self):
        kapoorrizzi.solve(self.kr, self.g_c_structure, self.g_c_copy, self.arbitrary_c, self.solution)

class python_timing_util(object):
    '''
    Functions for timing kr code Python implementation
    '''

    def __init__(self, degree, n_nodes):
        self.degree = degree
        self.KR = kapoor_rizzi()
    
        # generate random graph
        self.g = graph_util().generate_random_regular_bipartite(n_nodes, degree)
        
        # generate arbitrary partitions for approximation algo
        self.arbitrary = [graph_util().generate_random_regular_bipartite(n_nodes, 1) for i in xrange((degree % 2) + 1)]
    

    def solve(self):
        solution = self.KR.solve(self.degree, self.g, self.arbitrary);

        
    
