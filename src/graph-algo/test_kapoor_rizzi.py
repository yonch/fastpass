'''
Created on Oct 10, 2013

@author: yonch
'''
import unittest

from kapoor_rizzi import *
from graph_util import graph_util
import networkx as nx

class Test(unittest.TestCase):
    def test_approx_coloring(self):
        
        KR = kapoor_rizzi()
        
        degree = 40
        partition_n_nodes = 15
        
        # generate random graph
        g = graph_util().generate_random_regular_bipartite(partition_n_nodes, degree)
        
        # generate arbitrary partitions for approximation algo
        arbitrary = [graph_util().generate_random_regular_bipartite(partition_n_nodes, 1) for i in xrange((degree % 2) + 1)]
        
        # algorithm is destructive so save these for later comparisons
        original_nodes = g.nodes()
        arbitrary_edges = reduce(lambda x,y: x+y, (m.edges() for m in arbitrary))
        original_edges = g.edges() + arbitrary_edges
        
        solution = KR.solve(degree, g, arbitrary)
        
        # check the amount of matchings
        self.assertEqual(len(solution), degree + len(arbitrary), "Didn't get enough matchings")
        # check each matching:
        for matching in solution:
            # matching preserves nodes
            self.assertEquals(matching.nodes(), original_nodes)
            # every node has degree 1
            self.assertEquals(nx.degree_histogram(matching), [0, 2*partition_n_nodes])
        # matchings preserve edges
        matching_edges = reduce(lambda x,y: x+y, (m.edges() for m in solution))
        self.assertEquals(sorted(matching_edges), sorted(original_edges))#, "Mismatch between input and output edges")


if __name__ == "__main__":
    #import sys;sys.argv = ['', 'Test.testName']
    unittest.main()