use strict;
use warnings;

use Test::More;
use DBI;
use DBI::Const::GetInfoType;

use vars qw($test_dsn $test_user $test_password);
use lib 't', '.';
require 'lib.pl';

my @common_safe_methods = qw/
can                    err   errstr    parse_trace_flag    parse_trace_flags
private_attribute_info trace trace_msg
/;

# Not all DBI versions support method visit_child_handles
push @common_safe_methods, 'visit_child_handles' if DBI::db->can("visit_child_handles");

my @db_safe_methods   = (@common_safe_methods, qw/
clone mariadb_async_ready get_info quote_identifier type_info type_info_all last_insert_id
/);

my @db_unsafe_methods = qw/
data_sources       do                 selectrow_array
selectrow_arrayref selectrow_hashref  selectall_arrayref selectall_hashref
selectcol_arrayref prepare            prepare_cached     commit
rollback           begin_work         ping
table_info         column_info        primary_key_info   primary_key
foreign_key_info   statistics_info    tables             quote
/;

push @db_unsafe_methods, 'selectall_array' if DBI::db->can('selectall_array');

my @st_safe_methods   = qw/
fetchrow_arrayref fetch            fetchrow_array fetchrow_hashref
fetchall_arrayref fetchall_hashref finish         rows
last_insert_id
/;

my @st_unsafe_methods = qw/
bind_param bind_param_inout bind_param_array execute execute_array
execute_for_fetch bind_col bind_columns
/;

my %dbh_args = (
    can                 => ['can'],
    parse_trace_flag    => ['SQL'],
    parse_trace_flags   => ['SQL'],
    trace_msg           => ['message'],
    visit_child_handles => [sub { }],
    quote               => ['string'],
    quote_identifier    => ['Users'],
    do                  => ['SELECT 1'],
    last_insert_id      => [undef, undef, undef, undef],
    selectrow_array     => ['SELECT 1'],
    selectrow_arrayref  => ['SELECT 1'],
    selectrow_hashref   => ['SELECT 1'],
    selectall_array     => ['SELECT 1'],
    selectall_arrayref  => ['SELECT 1'],
    selectall_hashref   => ['SELECT 1', '1'],
    selectcol_arrayref  => ['SELECT 1'],
    prepare             => ['SELECT 1'],
    prepare_cached      => ['SELECT 1'],
    get_info            => [$GetInfoType{'SQL_DBMS_NAME'}],
    column_info         => [undef, undef, '%', '%'],
    primary_key_info    => [undef, undef, 'async_test'],
    primary_key         => [undef, undef, 'async_test'],
    foreign_key_info    => [undef, undef, 'async_test', undef, undef, undef],
    statistics_info     => [undef, undef, 'async_test', 0, 1],
);

my %sth_args = (
    fetchall_hashref  => [1],
    bind_param        => [1, 1],
    bind_param_inout  => [1, \(my $scalar = 1), 64],
    bind_param_array  => [1, [1]],
    execute_array     => [{ ArrayTupleStatus => [] }, [1]],
    execute_for_fetch => [sub { undef } ],
    bind_col          => [1, \(my $scalar2 = 1)],
    bind_columns      => [\(my $scalar3)],
);

my $dbh = DbiTestConnect($test_dsn, $test_user, $test_password,
                      { RaiseError => 0, PrintError => 0, AutoCommit => 0 });
plan skip_all => 'Async mode is not supported for Embedded server' if $dbh->{mariadb_hostinfo} eq 'Embedded';
plan tests =>
  2 * @db_safe_methods     +
  4 * @db_unsafe_methods   +
  7 * @st_safe_methods     +
  3 * @common_safe_methods +
  2 * @st_unsafe_methods   +
  3;

$dbh->do(<<SQL);
CREATE TEMPORARY TABLE async_test (
    value INTEGER
)
SQL

foreach my $method (@db_safe_methods) {
    $dbh->do('SELECT 1', { mariadb_async => 1 });
    my $args = $dbh_args{$method} || [];
    $dbh->$method(@$args);
    ok !$dbh->err, "Testing method '$method' on DBD::MariaDB::db during asynchronous operation";

    ok defined($dbh->mariadb_async_result);
}

$dbh->do('SELECT 1', { mariadb_async => 1 });
ok defined($dbh->mariadb_async_result);

foreach my $method (@db_unsafe_methods) {
    $dbh->do('SELECT 1', { mariadb_async => 1 });
    my $args = $dbh_args{$method} || [];
    my @values = $dbh->$method(@$args); # some methods complain unless they're called in list context
    like $dbh->errstr, qr/Calling a synchronous function on an asynchronous handle/, "Testing method '$method' on DBD::MariaDB::db during asynchronous operation";

    ok defined($dbh->mariadb_async_result);
}

foreach my $method (@common_safe_methods) {
    my $sth = $dbh->prepare('SELECT 1', { mariadb_async => 1 });
    $sth->execute;
    my $args = $dbh_args{$method} || []; # they're common methods, so this should be ok!
    $sth->$method(@$args);
    ok !$sth->err, "Testing method '$method' on DBD::MariaDB::db during asynchronous operation";
    ok defined($sth->mariadb_async_result);
    ok defined($sth->mariadb_async_result);
}

foreach my $method (@st_safe_methods) {
    my $sth = $dbh->prepare('SELECT 1', { mariadb_async => 1 });
    $sth->execute;
    my $args = $sth_args{$method} || [];
    $sth->$method(@$args);
    ok !$sth->err, "Testing method '$method' on DBD::MariaDB::st during asynchronous operation";

    # statement safe methods cache async result and mariadb_async_result can be called multiple times
    ok defined($sth->mariadb_async_result), "Testing DBD::MariaDB::st method '$method' for async result";
    ok defined($sth->mariadb_async_result), "Testing DBD::MariaDB::st method '$method' for async result";
}

foreach my $method (@st_safe_methods) {
    my $sync_sth  = $dbh->prepare('SELECT 1');
    my $async_sth = $dbh->prepare('SELECT 1', { mariadb_async => 1 });
    $dbh->do('SELECT 1', { mariadb_async => 1 });
    ok !$sync_sth->execute;
    ok $sync_sth->err;
    ok !$async_sth->execute;
    ok $async_sth->err;
    $dbh->mariadb_async_result;
}

foreach my $method (@db_unsafe_methods) {
    my $sth = $dbh->prepare('SELECT 1', { mariadb_async => 1 });
    $sth->execute;
    ok !$dbh->do('SELECT 1', { mariadb_async => 1 });
    ok $dbh->err;
    $sth->mariadb_async_result;
}

foreach my $method (@st_unsafe_methods) {
    my $sth = $dbh->prepare('SELECT value FROM async_test WHERE value = ?', { mariadb_async => 1 });
    $sth->execute(1);
    my $args = $sth_args{$method} || [];
    my @values = $sth->$method(@$args);
    like $dbh->errstr, qr/Calling a synchronous function on an asynchronous handle/, "Testing method '$method' on DBD::MariaDB::st during asynchronous operation";

    ok(defined $sth->mariadb_async_result);
}

my $sth = $dbh->prepare('SELECT 1', { mariadb_async => 1 });
$sth->execute;
ok defined($sth->mariadb_async_ready);
ok $sth->mariadb_async_result;

undef $sth;
$dbh->disconnect;
