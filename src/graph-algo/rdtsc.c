/*
 * Prints the value of the TSC
 */

#include <stdio.h>
#include "rdtsc.h"
#include "inttypes.h"

int main(int argc, char **argv)
{
  printf("%"PRIu64"\n", current_time());
}
