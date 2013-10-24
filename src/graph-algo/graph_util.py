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
    
    def generate_random_even_degree_bipartite(self, n, d, e):
        '''
    Generates a (probably) irregular bipartite graph of 2n nodes where each node
        has even degree of at most d and there are e edges total in the graph.
        e must be even and at most n * d.
    '''

        assert e % 2 == 0, 'e must be even'
        assert e <= n * d, 'e must be at most n * d'
       
        g = nx.MultiGraph()

        # Add 2n nodes to g
        for i in range(0, 2 * n):
            g.add_node(i)

        # Add e - 2 edges
        cur = random.randint(0, n-1)
        start = cur
        for i in range(0, e - 2, 2):
            next = random.randint(n, 2 * n - 1)
            while g.degree(next) == d:
                next = random.randint(n, 2 * n - 1)
            g.add_edge(cur, next)
            next_next = random.randint(0, n - 1)
            while g.degree(next_next) == d:
                next_next = random.randint(0, n - 1)
            g.add_edge(next, next_next)

            if (g.degree(next_next) % 2 == 0):
                cur = random.randint(0, n - 1)
                while g.degree(cur) >= d - 1:
                    cur = random.randint(0, n - 1)
            else:
                cur = next_next

        # Find left node with odd degree to start second to last edge
        for i in range(0, n):
            if (g.degree(i) % 2 == 1):
                left = i
                break
        else:
            # Find node with low enough degree
            for i in range(0, n):
                if (g.degree(i) < d):
                    left = i
        # Find right node with low enough degree
        for i in range(n, 2 * n):
            if (g.degree(i) < d):
                right = i
        # Add second-to-last-edge
        g.add_edge(left, right)
        # Find left node with odd degree to end last edge
        for i in range(0, n):
            if (g.degree(i) % 2 == 1):
                left = i
        g.add_edge(right, left)

        assert g.number_of_nodes() == 2 * n, 'graph constructed with wrong number of nodes'
        assert g.number_of_edges() == e, 'graph constructed with wrong number of edges'
        for i in range(2 * n):
            assert g.degree(i) % 2 == 0, 'graph constructed with odd degree vertex'
            assert g.degree(i) <= d, 'graph constructed with invalid degree'

        return g
        
