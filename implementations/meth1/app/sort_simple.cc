/**
 * Test program. Takes gensort file as input and reads whole thing into memory,
 * sorting using C++ algorithm sort implementation.
 */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include "exception.hh"
#include "util.hh"

using namespace std;

// sizes of records
static constexpr size_t KEY_BYTES = 10;
static constexpr size_t VAL_BYTES = 90;
static constexpr size_t REC_BYTES = KEY_BYTES + VAL_BYTES;
static constexpr size_t NRECS = 10485706;

// large array for record values
static char * all_vals = new char[ NRECS * VAL_BYTES ];
static char * cur_v = all_vals;

// our record struct
struct Rec
{
  // Pulling the key inline with Rec improves sort performance by 2x. Appears
  // to be due to saving an indirection during sort.
  char   key_[10];

  char * val_;
  size_t loc_;

  Rec( char * r, size_t loc ) : val_{r+KEY_BYTES}, loc_{loc}
  {
    memcpy( key_, r, KEY_BYTES );
    memcpy( cur_v, val_, VAL_BYTES );
    val_ = cur_v;
    cur_v += VAL_BYTES;
  }

  /* Comparison */
  bool operator<( const Rec & b ) const
  {
    return compare( b ) < 0 ? true : false;
  }

  /* we compare on key first, and then on diskloc_ */
  int compare( const Rec & b ) const
  {
    int cmp = memcmp( key_, b.key_, 10 );
    if ( cmp == 0 ) {
      if ( loc_ < b.loc_ ) { cmp = -1; }
      else if ( loc_ > b.loc_ ) { cmp = 1; }
    }
    return cmp;
  }

  size_t write( FILE *fdo )
  {
    size_t n = 0;
    n += fwrite( key_, KEY_BYTES, 1, fdo );
    n += fwrite( val_, VAL_BYTES, 1, fdo );
    return n;
  }
};

int run( char * argv[] )
{
  // startup
  FILE *fdi = fopen( argv[1], "r" );
  FILE *fdo = fopen( argv[2], "w" );

  vector<Rec> recs{};
  recs.reserve( NRECS );

  auto t1 = chrono::high_resolution_clock::now();

  // read
  char r[REC_BYTES];
  for ( uint64_t i = 0;; i++ ) {
    size_t n = fread( r, REC_BYTES, 1, fdi );
    if ( n == 0 ) {
      break;
    }
    recs.emplace_back( r, i );
  }
  fclose( fdi );
  auto t2 = chrono::high_resolution_clock::now();

  // sort
  sort( recs.begin(), recs.end() );
  auto t3 = chrono::high_resolution_clock::now();

  // write
  for ( auto & r : recs ) {
    r.write( fdo );
  }
  fflush( fdo );
  fsync( fileno( fdo ) );
  fclose( fdo );
  auto t4 = chrono::high_resolution_clock::now();

  // stats
  auto t21 = chrono::duration_cast<chrono::milliseconds>( t2 - t1 ).count();
  auto t32 = chrono::duration_cast<chrono::milliseconds>( t3 - t2 ).count();
  auto t43 = chrono::duration_cast<chrono::milliseconds>( t4 - t3 ).count();
  auto t41 = chrono::duration_cast<chrono::milliseconds>( t4 - t1 ).count();

  cout << "Read  took " << t21 << "ms" << endl;
  cout << "Sort  took " << t32 << "ms" << endl;
  cout << "Write took " << t43 << "ms" << endl;
  cout << "Total took " << t41 << "ms" << endl;

  return EXIT_SUCCESS;
}

void check_usage( const int argc, const char * const argv[] )
{
  if ( argc != 3 ) {
    throw runtime_error( "Usage: " + string( argv[0] ) + " [file] [out]" );
  }
}

int main( int argc, char * argv[] )
{
  try {
    check_usage( argc, argv );
    run( argv );
  } catch ( const exception & e ) {
    print_exception( e );
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
