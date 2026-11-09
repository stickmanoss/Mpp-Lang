// C++ baseline for io_write.mpp (iostreams, sync_with_stdio(false)).
#include <iostream>
int main(){ std::ios::sync_with_stdio(false); for(int i=0;i<5000000;i++) std::cout<<i<<'\n'; }
