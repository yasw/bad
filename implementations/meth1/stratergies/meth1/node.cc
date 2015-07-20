#include <fcntl.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <system_error>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

#include <boost/sort/spreadsort/string_sort.hpp>

#include "address.hh"
#include "buffered_io.hh"
#include "exception.hh"
#include "file.hh"
#include "socket.hh"

#include "record.hh"

#include "node.hh"
#include "priority_queue.hh"

#define USE_OVERLAP_IO 0

using namespace std;
using namespace meth1;
using namespace boost::sort::spreadsort;

// Check size_t for a vector is a size_type or larger
static_assert( sizeof( vector<Record>::size_type ) >= sizeof( uint64_t ),
               "Vector<Record> max size is too small!" );

/* Construct Node */
Node::Node( string file, string port, uint64_t max_mem )
  : data_{{file.c_str(), O_RDONLY}}
  , port_{port}
  , last_{Record::MIN}
  , fpos_{0}
  , max_mem_{max_mem}
  , mtx_{}
  , cv_{}
{
}

/* run the node - list and respond to RPCs */
void Node::Run( void )
{
  TCPSocket sock{IPV4};
  sock.set_reuseaddr();
  sock.bind( {"0.0.0.0", port_} );
  sock.listen();

  while ( true ) {
    try {
      BufferedIO_O<TCPSocket> client {sock.accept()};

      while ( true ) {
        const char * str = client.read_buf_all( 1 ).first;
        if ( client.eof() ) {
          break;
        }

        switch ( str[0] ) {
        case 0: // Read
          RPC_Read( client );
          break;
        case 1: // Size
          RPC_Size( client );
          break;
        default:
          throw runtime_error( "Unknown RPC method: " + to_string(str[0]) );
          break;
        }
      }
    } catch ( const exception & e ) {
      print_exception( e );
    }
  }
}

void Node::RPC_Read( BufferedIO_O<TCPSocket> & client )
{
  // parse arguments
  const char * str = client.read_buf_all( 2 * sizeof( uint64_t ) ).first;
  uint64_t pos = *( reinterpret_cast<const uint64_t *>( str ) );
  uint64_t amt = *( reinterpret_cast<const uint64_t *>( str ) + 1 );

  // perform read
  vector<Record> recs = Read( pos, amt );
  uint64_t siz = recs.size();

  // serialize results to wire
  client.write_all( reinterpret_cast<const char *>( &siz ), sizeof( uint64_t ) );
  for ( auto const & r : recs ) {
    r.write( client );
  }
  client.flush( true );
}

void Node::RPC_Size( BufferedIO_O<TCPSocket> & client )
{
  uint64_t siz = Size();
  client.write_all( reinterpret_cast<const char *>( &siz ), sizeof( uint64_t ) );
  client.flush( true );
}

/* Initialization routine */
void Node::DoInitialize( void ) { return; }

/* Read a contiguous subset of the file starting from specified position. */
vector<Record> Node::DoRead( uint64_t pos, uint64_t size )
{
  Record after = seek( pos );

  // read the records from disk
  auto recs = linear_scan( after, size );
  if ( recs.size() > 0 ) {
    last_ = recs.back();
    fpos_ = pos + recs.size();
  }

  return recs;
}

/* Return the the number of records on disk */
uint64_t Node::DoSize( void )
{
  return data_.io().size() / Record::SIZE;
}

/* seek returns the record that occurs just before `pos` so it can be passed to
 * `linear_scan` */
Record Node::seek( uint64_t pos )
{
  cout << "- Seek (" << pos << ")" << endl;
  auto start = chrono::high_resolution_clock::now();

  Record after{Record::MIN};

  if ( fpos_ == pos ) {
    // continuing from last time
    after = last_;
  } else {
    // remember, retrieving the record just before `pos`
    for ( uint64_t i = 0; i < pos; i += max_mem_ ) {
      auto recs = linear_scan( after, min( pos - i, max_mem_ ) );
      if ( recs.size() == 0 ) {
        break;
      }
      after = recs.back();
    }
  }

  // seek timings
  auto end = chrono::high_resolution_clock::now();
  auto dur = chrono::duration_cast<chrono::milliseconds>( end - start ).count();
  cout << "* Seek took: " << dur << "ms" << endl;

  return after;
}

void Node::read_file( void )
{
  wptr_ = buf_;
  while ( true ) {
    size_t n = data_.io().read_all( wptr_, BLOCK );

    wptr_ += BLOCK;
    if ( wptr_ == buf_ + BUF_SIZE ) {
      wptr_ = buf_;
    }

    unique_lock<mutex> lck( mtx_ );
    last_block_ = n;
    blocks_++;
    cv_.notify_one(); // notify reader

    if ( data_.io().eof() ) {
      break;
    } else {
      // wait on reader
      cv_.wait( lck, [&]() { return blocks_ < NBLOCKS; } );
    }
  }
}

char * Node::chunk( size_t &siz )
{
  unique_lock<mutex> lck( mtx_ );
  // wait on writer
  cv_.wait( lck, [&]() { return blocks_ > 0; } );
  siz = data_.io().eof() and blocks_ == 1 ? last_block_ : BLOCK;
  return rptr_;
}

bool Node::next_chunk( void )
{
  rptr_ += BLOCK;
  if ( rptr_ == buf_ + BUF_SIZE ) {
    rptr_ = buf_;
  }

  unique_lock<mutex> lck( mtx_ );
  blocks_--;
  cv_.notify_one(); // notify writer

  return data_.io().eof() && blocks_ == 0;
}

#if USE_OVERLAP_IO == 1

/* Perform a full linear scan to return the next smallest record that occurs
 * after the 'after' record. */
vector<Record> Node::linear_scan( const Record & after, uint64_t size )
{
  static uint64_t pass = 0;

  // lineaer scan
  cout << "- Linear scan " << pass++ << " (" << size << ")" << endl;
  auto start = chrono::high_resolution_clock::now();

  thread reader ( &Node::read_file, this );

  mystl::priority_queue<Record> recs{size + 1};

  uint64_t i = 0;
  size_t n = 0;
  char r[Record::SIZE];
  char * block = chunk( n );
  char * pr = block;

  do {
    for (; pr <= block + n - Record::SIZE; pr += Record::SIZE ) {
      Record next = Record( pr, i++ );
      if ( after < next ) {
        if ( recs.size() < size ) {
          recs.push( next );
        } else if ( next < recs.top() ) {
          recs.push( next );
          recs.pop();
        }
      }
    }

    // copy trailing, ready for next block
    size_t trail = n - (pr - block);
    if ( trail > 0 ) {
      memcpy( r, pr, trail );
    }

    // get next block
    if ( next_chunk() ) {
      break;
    }
    block = chunk( n );
    pr = block;

    // complete partial record
    if ( trail > 0 ) {
      memcpy( r + trail, pr, Record::SIZE - trail );
      pr += Record::SIZE - trail;

      Record next = Record( r, i++ );
      if ( after < next ) {
        if ( recs.size() < size ) {
          recs.push( next );
        } else if ( next < recs.top() ) {
          recs.push( next );
          recs.pop();
        }
      }
    }
  } while ( true );

  reader.join();

  // timings -- scan
  auto split = chrono::high_resolution_clock::now();
  auto dur   = chrono::duration_cast<chrono::milliseconds>( split - start ).count();
  cout << "* Linear scan took: " << dur << "ms" << endl;

  // rewind the file
  data_.io().rewind();

  // sort final heap
  vector<Record> vrecs {move( recs.container() )};
  string_sort( vrecs.begin(), vrecs.end() );

  // timings -- sort
  auto end = chrono::high_resolution_clock::now();
  dur = chrono::duration_cast<chrono::milliseconds>( end - split ).count();
  cout << "* In-memory sort took: " << dur << "ms" << endl;

  // timings -- total
  dur = chrono::duration_cast<chrono::milliseconds>( end - start ).count();
  cout << "* Read took: " << dur << "ms" << endl;

  return vrecs;
}

#else /* !USE_OVERLAP_IO */

vector<Record> Node::linear_scan( const Record & after, uint64_t size )
{
  static uint64_t pass = 0;

  cout << "- Linear scan " << pass++ << " (" << size << ")" << endl;
  auto start = chrono::high_resolution_clock::now();

  mystl::priority_queue<Record> recs{size + 1};

  for ( uint64_t i = 0;; i++ ) {
    const char * rec_str = data_.read_buf_all( Record::SIZE ).first;
    Record next = Record( rec_str, i );
    if ( data_.eof() ) {
      break;
    }

    if ( after < next ) {
      if ( recs.size() < size ) {
        recs.push( next );
      } else if ( next < recs.top() ) {
        recs.push( next );
        recs.pop();
      }
    }
  }

  // timings -- scan
  auto split = chrono::high_resolution_clock::now();
  auto dur   = chrono::duration_cast<chrono::milliseconds>( split - start ).count();
  cout << "* Linear scan took: " << dur << "ms" << endl;

  // rewind the file
  data_.io().rewind();

  // sort final heap
  vector<Record> vrecs {move( recs.container() )};
  string_sort( vrecs.begin(), vrecs.end() );

  // timings -- sort
  auto end = chrono::high_resolution_clock::now();
  dur = chrono::duration_cast<chrono::milliseconds>( end - split ).count();
  cout << "* In-memory sort took: " << dur << "ms" << endl;

  // timings -- total
  dur = chrono::duration_cast<chrono::milliseconds>( end - start ).count();
  cout << "* Read took: " << dur << "ms" << endl;

  return vrecs;
}

#endif /* USE_OVERLAP_IO */
