'''
Created on October 23, 2013

@author: aousterh
'''
import sys
import unittest

sys.path.insert(0, '../../bindings/graph-algo')
sys.path.insert(0, '../../src/graph-algo')

import networkx as nx
from graph_util import graph_util
import random
import eulersplit
import graph

class Test(unittest.TestCase):

    def test_keeps_edge_set(self):
        generator = graph_util()
        for deg in xrange(2, 11, 2):
            for n_side in xrange(2*deg+4,33,7):
                print("testing with n=%d, degree=%d" % (n_side, deg))

                g_p = generator.generate_random_regular_bipartite(n_side, deg)
                
                # Create the graph in C
                g_c = graph.create_graph_test(n_side);
                g_c_copy = graph.create_graph_test(n_side)
                for edge in g_p.edges_iter():
                    graph.add_edge(g_c, edge[0], edge[1])
                    graph.add_edge(g_c_copy, edge[0], edge[1])
                self.assertEqual(graph.get_max_degree(g_c), deg)
                self.assertEqual(graph.get_max_degree(g_c_copy), deg)
             
                # Create the output graphs
                g1_c = graph.create_graph_test(n_side)
                g2_c = graph.create_graph_test(n_side)

                eulersplit.split(g_c_copy, g1_c, g2_c)

                # Check that the input graph is now empty
                self.assertEqual(graph.get_max_degree(g_c_copy), 0)
                for node in xrange(2 * n_side):
                    self.assertEqual(graph.get_degree(g_c_copy, node), 0)

                # Check that graphs have the correct degree
                self.assertEqual(graph.get_max_degree(g1_c), deg / 2)
                self.assertEqual(graph.get_max_degree(g2_c), deg / 2)

                # Check that vertices have the correct degree
                for node in xrange(2 * n_side):
                    self.assertEqual(graph.get_degree(g1_c, node), deg / 2)
                    self.assertEqual(graph.get_degree(g2_c, node), deg / 2)

                # Check that the combination of the two graphs equals the original graph
                graph.add_graph(g1_c, g2_c)
                self.assertTrue(graph.are_equal(g_c, g1_c))
                   
                graph.destroy_graph_test(g_c)
                graph.destroy_graph_test(g_c_copy)
                graph.destroy_graph_test(g1_c)
                graph.destroy_graph_test(g2_c)

        pass

        # TODO: write more tests, especially for non-regular graphs.

if __name__ == "__main__":
    unittest.main()
