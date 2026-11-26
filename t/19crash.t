#!/usr/bin/perl
use warnings;
use strict;

use DBI;

my $dbh = DBI->connect("DBI:MariaDB:", "test", undef, {RaiseError=>1});
my $sth = $dbh->prepare("SELECT ?");
$dbh->disconnect();
$sth->execute("1");
