'''
Created on Oct 9, 2013

@author: yonch
'''

import random
import networkx as nx
from networkx.generators import bipartite

class graph_util(object):
    '''
    Generates different bipartite graphs
    '''

    def generate_random_regular_bipartite(self, n, d):
        '''
    Generates a regular bipartite graph of 2n nodes where each node has degree d
    '''
        g = nx.MultiGraph()
        bipartite._add_nodes_with_bipartite_label(g, n, n)
        left_nodes = range(n)
        right_nodes = range(n, 2*n)
        
        for i in xrange(d):
            random.shuffle(right_nodes)
            g.add_edges_from(zip(left_nodes, right_nodes))
        
        return g
    
