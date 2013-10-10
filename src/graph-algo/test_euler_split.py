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


if __name__ == "__main__":
    #import sys;sys.argv = ['', 'Test.test_keeps_edge_set']
    unittest.main()