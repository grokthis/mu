# First example: return the answer to the Ultimate Question of Life, the
# Universe, and Everything.
#
# To run:
#   $ ./translate_mu apps/ex1.mu
#   $ ./a.elf
# Expected result:
#   $ echo $?
#   42

fn main -> result/ebx: int {
  result <- copy 0x2a  # Mu requires hexadecimal
}
