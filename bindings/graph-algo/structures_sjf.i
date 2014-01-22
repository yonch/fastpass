%module structuressjf

%{
#include "../../src/protocol/topology.h"
#include "../../src/graph-algo/admissible_structures_sjf.h"
%}

%include "stdint.i"

#define __attribute__(x)

%include "../../src/protocol/topology.h"
%include "../../src/graph-algo/admissible_structures_sjf.h"
