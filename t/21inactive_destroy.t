use strict;
use warnings;

use Test::More;
use DBI;

use vars qw($test_dsn $test_user $test_password);
use lib 't', '.';
require 'lib.pl';

sub connect_test_dbh {
    return DbiTestConnect(
        $test_dsn,
        $test_user,
        $test_password,
        { RaiseError => 1, PrintError => 0 },
    );
}

# Explicit disconnect() must disconnect regardless of where InactiveDestroy
# is set.
{
    my $dbh = connect_test_dbh();
    $dbh->{InactiveDestroy} = 1;

    ok($dbh->disconnect(), 'disconnect() with InactiveDestroy on dbh succeeds');
    ok(!$dbh->{Active}, 'disconnect() deactivates dbh');
}

{
    my $dbh = connect_test_dbh();
    my $sth = $dbh->prepare('SELECT 1');
    $sth->{InactiveDestroy} = 1;

    ok($dbh->disconnect(), 'disconnect() with InactiveDestroy on sth succeeds');
    ok(!$dbh->{Active}, 'disconnect() deactivates dbh when attribute is on sth');
}

# Explicit disconnect_all() must also disconnect regardless of where
# InactiveDestroy is set.
{
    my $dbh = connect_test_dbh();
    $dbh->{InactiveDestroy} = 1;

    DBI->disconnect_all();
    ok(!$dbh->{Active}, 'disconnect_all() deactivates dbh with attribute on dbh');
}

{
    my $dbh = connect_test_dbh();
    my $sth = $dbh->prepare('SELECT 1');
    $sth->{InactiveDestroy} = 1;

    DBI->disconnect_all();
    ok(!$dbh->{Active}, 'disconnect_all() deactivates dbh with attribute on sth');
}

SKIP: {
    skip 'fork is not available', 6 unless defined(my $fork_check = fork());

    # This first fork only checks availability; reap it before the real tests.
    if ($fork_check == 0) {
        exit 0;
    }
    waitpid($fork_check, 0);

    # Suppress Test::More's inherited end-of-process output in each child.

    # DBI's own END cleanup must respect InactiveDestroy on the dbh.
    {
        my $dbh = connect_test_dbh();
        my $pid = fork();
        die "fork failed: $!" unless defined $pid;

        if ($pid == 0) {
            $dbh->{InactiveDestroy} = 1;
            Test::More->builder->no_ending(1);
            exit 0; # DBI's END block runs in the child.
        }

        waitpid($pid, 0);
        is($? >> 8, 0, 'child exits cleanly with InactiveDestroy on dbh');
        ok($dbh->{Active}, 'parent dbh remains active after child END');
        is($dbh->selectrow_array('SELECT 1'), 1,
            'parent can query after child END with InactiveDestroy on dbh');
    }

    # DBI's own END cleanup must respect InactiveDestroy on the sth.
    {
        my $dbh = connect_test_dbh();
        my $sth = $dbh->prepare('SELECT 1');
        my $pid = fork();
        die "fork failed: $!" unless defined $pid;

        if ($pid == 0) {
            $sth->{InactiveDestroy} = 1;
            exit 0; # DBI's END block runs in the child.
        }

        waitpid($pid, 0);
        is($? >> 8, 0, 'child exits cleanly with InactiveDestroy on sth');
        ok($dbh->{Active}, 'parent dbh remains active after child END');
        ok($sth->execute(), 'parent can execute sth after child END');
    }
}

done_testing;
