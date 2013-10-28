'''
Created on October 27, 2013

@author: aousterh
'''
import sys
import unittest

sys.path.insert(0, '../../src/graph-algo')

from kr_util import kr_util

class Test(unittest.TestCase):

    def test(self):
        generator = kr_util()
        generator.build_kr(15, 40)

        pass

if __name__ == "__main__":
    unittest.main()
