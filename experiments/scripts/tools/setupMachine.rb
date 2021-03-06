#!/usr/bin/env ruby

#
# This script setups a newly provisioned EC2 machine to run B.A.D.
#

require 'net/ssh'
require 'net/scp'
require './lib/setup'

if ARGV.length >= 1 &&
    (ARGV[0] == "help" || ARGV[0] == "--help" || ARGV[0] == "-h")
  puts "Usage: " + __FILE__ + " <host>"
  puts "      <host>     -- The host to connect to. "
  exit 0
end

opts = { interactive: true, host: ARGV[0] }
Setup.new(opts).setup!

puts "Setup done!"
puts ""
