'''
Created on October 22, 2013

@author: aousterh
'''
import sys
import unittest

sys.path.insert(0, '../../bindings/graph-algo')
sys.path.insert(0, '../../src/graph-algo')

import networkx as nx
from graph_util import graph_util
import random
import graph

class Test(unittest.TestCase):

    def test_graph_creation(self):
        generator = graph_util()
        
        for deg in xrange(2, 11, 2):
            for n_side in xrange(2*deg+4,33,7):
                g_p = generator.generate_random_regular_bipartite(n_side, deg)

                g_c_structure = graph.create_graph_structure_test(n_side)
                g_c = graph.create_graph_edges_test(n_side)
                
                # Create the graph in C
                # first n vertices are on left, second n are on right
                for edge in g_p.edges_iter():
                    graph.add_edge(g_c_structure, g_c, edge[0], edge[1])
                    
                # Check that graph in C matches the graph in Python
                for node in xrange(2 * n_side):
                    self.assertEqual(g_p.degree(node), graph.get_degree(g_c, node))
                
                for node in xrange(2 * n_side):
                    if (g_p.degree(node) > 0):
                        self.assertTrue(graph.has_neighbor(g_c, node))

                graph.destroy_graph_structure_test(g_c_structure)
                graph.destroy_graph_edges_test(g_c)
        pass

        # TODO: could write more tests
                        

if __name__ == "__main__":
    unittest.main()
