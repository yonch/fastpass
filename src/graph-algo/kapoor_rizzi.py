'''
Created on Oct 9, 2013

@author: yonch
'''

from euler_split import euler_split

class bin_graph(object):
    def __init__(self, degree, graph):
        self.degree = degree
        self.g = graph

class kapoor_rizzi(object):
    '''
    Implements the Kapoor-Rizzi method to reduce bipartite multigraph edge 
        coloring to multiple Euler-splits and at most one matching search.
    '''
    
    def __init__(self):
        self.euler_split = euler_split()

    def almost_solve(self, d, g):
        print "Normalize:"
        bins = self._normalize(bin_graph(d,g))
        print "After normalize", bins
        
        a = bins[0]
        b = bins[1]
        c = bins[2]
        assert (c.degree == b.degree)
        T = bins[3:]

        while (a.degree != b.degree):
            while (b.degree % 2 == 0):
                T = [c] + T
                b, c = self._split_even(b)
                
            self._print_degrees([a,b,c] + T)
            
            if (a != b):
                a,b,c = self._hit_even(a, b, c)
                self._print_degrees([a,b,c] + T)
        
        return [a,b,c] + T
    
    def _normalize(self, d):
        bins = []
        
        if d.degree < 3:
            raise RuntimeError, "need d >= 3"
        
        while d.degree % 2 == 0:
            assert(d.degree >= 3)
            if d.degree == 4:
                print "d/2 == 2"
                g_a, g_b = self._split_even(d)
                g1,g2 = self._split_even(g_a, 2)
                g3,g4 = self._split_even(g_b, 2)
                return [g1,g2,g3,g4] + bins
                
            d,d1 = self._split_even(d)
            bins = [d1] + bins
            
        assert(d >= 3)
        print "Slice-one(%d)" % d
        print "Split-even(%d)" % (d - 1)
        return [1, d/2, d/2] + bins
    
    def _hit_even(self, g0, g1, g2):
        '''
        perform HIT-EVEN on (a,b,b)
        '''
        a = g0[0]
        b = g1[0]
        print "Hit-even(%d,%d,%d)" % (a,b,b)
        
        assert(a % 2 == 1)
        assert(b % 2 == 1)
        assert(a != b)
        assert(g2[0] == b)
        
        while g2[0] % 2 == 1:
            (g0, (g1, g2)) = (g2, self._split_odd(g0, g1)) 
            (a,b) = (b, ((a + b) / 2))
        
        return a,b,g0,g1,g2
    
    def _split_even(self, d):
        print "Split-even(%d)" % d.degree 
        g1,g2 = self.euler_split.split(d.g)
        return bin_graph(d.degree/2, g1), bin_graph(d.degree/2, g2)
    
    def _split_odd(self, g1, g2):
        print "Split-odd(%d,%d)" % (g1.degree,d2.degree)
        g1[1].add_edges_from(g2[1].edges())
        d1 += d2
        g_a, g_b = self.euler_split.split(g1[1])
        return (d1/2, g_a), (d1/2, g_b)
    
    def _print_degrees(self, L):
        print ", ".join(repr(x.degree) for x in L)