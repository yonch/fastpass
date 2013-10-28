'''
Created on October 27, 2013

@author: aousterh
'''

import sys
import unittest

sys.path.insert(0, '../../bindings/graph-algo')

import kapoorrizzi

class bin_info(object):
    def __init__(self, degree, index):
        self.degree = degree
        self.index = index

class kr_util(object):
    '''
    Creates and initializes a KR for a specific network topology.
    Based on kapoor_rizzi.py, but doesn't actually manipulate graphs.
    '''

    def build_kr(self, n, d):
        '''
    Builds a kr for a network with n nodes each with degree d.
    Assumes d is even and that we use the approximation method.
    '''
        assert d % 2 == 0, "build_kr assumes even degree, given odd degree"

        self.num_matchings = d + 1

        # track which indices are currently being used
        # first num_matchings indices are for matchings, remaining are for work space
        self.indices = []
        for i in range(2 * self.num_matchings):
            self.indices.append(0)

        # assume solving begins with an arbitrary matching at index 0 and the degree d
        # graph at index num_matchings
        self.indices[0] = 1
        self.indices[self.num_matchings] = 1

        self.kr = kapoorrizzi.create_kr(d)

        bins = self._solve(d)

        return self.kr

    def _solve(self, d):
        bins = self._almost_solve(d)
        
        matchings = []
        work_list = bins
        work_list.reverse() # want to use pop and append, so need to reverse

        while (len(work_list) > 0):
            g = work_list.pop()
            while (g.degree != 1):
                self._print_degrees(matchings + [g] + list(reversed(work_list)))
                if (g.degree % 2 == 1):
                    # add a matching, and split
                    g, g1 = self._split_odd(g, matchings.pop())
                else:
                    # just split
                    g, g1 = self._split_even(g)
                # save g1 for later
                work_list.append(g1)
            # okay, got a matching
            matchings.append(g)
        
        self._print_degrees(matchings)
        
        return matchings
            
    def _almost_solve(self, d):
        '''
        Performs ALMOST-SOLVE(delta).
        '''
        print "Normalize:"
        bins = self._approx_normalize(bin_info(d, self.num_matchings), bin_info(1, 0))
        print "After normalize"
        self._print_degrees(bins)

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
            
            if (a.degree != b.degree):
                a,b,c = self._hit_even(a, b, c)
                self._print_degrees([a,b,c] + T)
        
        return [a,b,c] + T

    def _approx_normalize(self, d, arbitrary_matching):
        g1, g2 = self._split_even(d)
        return [arbitrary_matching, g1, g2]

    def _hit_even(self, a, b, c):
        '''
        perform HIT-EVEN on (a,b,c)
        '''
        print "Hit-even(%d,%d,%d)" % (a.degree,b.degree,c.degree)
        
        assert(a.degree % 2 == 1)
        assert(b.degree % 2 == 1)
        assert(a.degree != b.degree)
        assert(b.degree == c.degree)
        
        while b.degree % 2 == 1:
            if (a.degree >= b.degree):
                (a, (b, c)) = (c, self._split_odd(a, b)) 
            else:
                (a, (b, c)) = (c, self._split_odd(b, a)) 
         
        return a, b, c

    def _split_even(self, d):
        print "Split-even(%d)" % d.degree
        
        if d.degree == 2:
            # split into matchings area
            index1 = self._get_free_matching_index()
            self.indices[index1] = 1
            index2 = self._get_free_matching_index()
            self.indices[index2] = 1
        else:
            # split into workspace area
            index1 = self._get_free_workspace_index()
            self.indices[index1] = 1
            index2 = self._get_free_workspace_index()
            self.indices[index2] = 2

        self.indices[d.index] = 0
  
        # record this step
        # print "kr step split even (%d,%d,%d)" % (d.index, index1, index2)
        kapoorrizzi.set_kr_step(self.kr, kapoorrizzi.SPLIT_EVEN,
                                d.index, 0, index1, index2);

        return bin_info(d.degree / 2, index1), bin_info(d.degree / 2, index2)

    def _split_odd(self, d1, d2):
        print "Split-odd(%d,%d)" % (d1.degree,d2.degree)
        
        d1.degree += d2.degree
        index1 = self._get_free_workspace_index()
        self.indices[index1] = 1
        index2 = self._get_free_workspace_index()
        self.indices[index2] = 1

        self.indices[d1.index] = 0
        self.indices[d2.index] = 0

        # record this step
        # print "kr step split odd (%d,%d,%d,%d)" % (d1.index, d2.index, index1, index2)
        kapoorrizzi.set_kr_step(self.kr, kapoorrizzi.SPLIT_ODD,
                                d1.index, d2.index, index1, index2);

        return bin_info(d1.degree / 2, index1), bin_info(d1.degree / 2, index2)

    def _get_free_matching_index(self):
        for i in range(self.num_matchings):
            if self.indices[i] == 0:
                return i

    def _get_free_workspace_index(self):
        for i in range(self.num_matchings):
            if self.indices[self.num_matchings + i] == 0:
                return self.num_matchings + i
        
    def _print_degrees(self, L):
        print ", ".join(repr(x.degree) for x in L)
