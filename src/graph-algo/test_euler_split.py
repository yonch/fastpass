'''
Created on Oct 9, 2013

@author: yonch
'''
import unittest

import networkx as nx
from euler_split import euler_split
from graph_util import graph_util
import random

class Test(unittest.TestCase):

    def test_keeps_edge_set(self):
        ES = euler_split()
        generator = graph_util()
        for deg in xrange(2, 11, 2):
            for n_side in xrange(2*deg+4,33,7):
                g = generator.generate_random_regular_bipartite(n_side, deg)
                g1, g2 = ES.split(g.copy())
                
                self.assertEquals(g1.nodes(), g.nodes())
                self.assertEquals(g2.nodes(), g.nodes())
                self.assertEqual(len(g.edges()), 2 * len(g1.edges()))
                self.assertEqual(len(g.edges()), 2 * len(g2.edges()))
                self.assertEquals(sorted(g.edges()), sorted(g1.edges() + g2.edges()))
                self.assertEquals([g1.degree(x) for x in g1.nodes()], [deg/2]*2*n_side)
                self.assertEquals([g2.degree(x) for x in g2.nodes()], [deg/2]*2*n_side)
                
        pass

    def test_irregular_graphs(self):
        ES = euler_split()
        generator = graph_util()

        for max_deg in xrange(2, 11, 2):
            for n_side in xrange(2 * max_deg + 4, 33, 7):
                for e in xrange(4, n_side * max_deg - 4, 10):
                
                    g = generator.generate_random_even_degree_bipartite(n_side, max_deg, e)
                    g1, g2 = ES.split(g.copy())
        
                    # Add nodes, since they might have degree 0
                    for x in g.nodes():
                        g1.add_node(x)
                        g2.add_node(x)

                    self.assertEqual(g1.number_of_nodes(), g.number_of_nodes())
                    self.assertEqual(g2.number_of_nodes(), g.number_of_nodes())
                    self.assertEqual(len(g.edges()), 2 * len(g1.edges()))
                    self.assertEqual(len(g.edges()), 2 * len(g2.edges()))
                    self.assertEquals(sorted(g.edges()), sorted(g1.edges() + g2.edges()))
                    self.assertEquals([g1.degree(x) + g2.degree(x) for x in g.nodes()],
                                      [g.degree(x) for x in g.nodes()])
                
        pass

if __name__ == "__main__":
    #import sys;sys.argv = ['', 'Test.test_keeps_edge_set']
    unittest.main()
