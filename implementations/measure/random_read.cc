#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib.hh"

int main(int argc, char* argv[]) {
  if (argc != 3 && argc != 4) {
    printf("Usage: ./read [file] [block size] ([O_DIRECT?])\n");
    return EXIT_FAILURE;
  }
  bool odirect = argc == 4;
  int flags = odirect ? O_DIRECT | O_RDONLY : O_RDONLY;

  const char* file_name = argv[1];
  int fd = open(file_name, flags);
  if (fd < 0) {
    perror("Failed when opening file");
    return EXIT_FAILURE;
  }

  struct stat stat_buffer;
  if (fstat(fd, &stat_buffer) != 0) {
    perror("Failed when fstat.");
    return EXIT_FAILURE;
  }
  size_t file_size = stat_buffer.st_size;
  size_t block_size = atol(argv[2]);
  size_t num_block = file_size / block_size;

  size_t* random_block = new size_t[num_block];
  FillRandomPermutation(random_block, num_block);

  struct timespec time_start, time_end;
  get_timespec(&time_start);

  void * data;
  if (posix_memalign((void **)&data, ALIGN, block_size) != 0) {
      return EXIT_FAILURE;
  }

  for (size_t i = 0; i < num_block; ++i) {
    ssize_t r = pread(fd, data, block_size, random_block[i] * block_size);
    if (r == -1) {
      perror("Failed when reading");
    } else if ( r == 0 ) {
      break; // EOF
    }
  }

  get_timespec(&time_end);

  double dur = time_diff(time_start, time_end);
  double mbs = file_size / dur / MB;
  // read?, sync?, random?, block, depth, odirect, MB/s
  printf("%c, %c, %c, %ld, %d, %d, %f\n", 'r', 's', 'r', block_size, 1, odirect, mbs);

  if (close(fd) != 0) {
    perror("Failed when closing.");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
