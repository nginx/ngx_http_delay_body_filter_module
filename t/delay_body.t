#!/usr/bin/perl

# (C) Maxim Dounin

# Tests for delay body filter module.

###############################################################################

use warnings;
use strict;

use Test::More;
use Test::Nginx;

use Socket qw/ CRLF /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy rewrite/)
	->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location / {
            delay_body 1s;
            add_header X-Time $request_time;
            proxy_pass http://127.0.0.1:8080/empty;
        }
        location /empty {
            return 200 "test response body\n";
        }
    }
}

EOF

$t->try_run('no delay_body')->plan(4);

###############################################################################

like(get_body('/', '123456'), qr/200 OK.*X-Time: 1/ms, 'delay');
like(get_body('/empty', '123456'), qr/200 OK.*X-Time: 0/ms, 'no delay');

# pipelining

like(get_body('/', '123456', '12345X'),
	qr/200 OK.*X-Time: 1.*200 OK.*X-Time: 1/ms,
	'pipelining delay');

# pipelining with chunked

like(get_chunked('/', '123456', '12345X'),
	qr/200 OK.*X-Time: 1.*200 OK.*X-Time: 1/ms,
	'pipelining chunked delay');

###############################################################################

sub get_body {
	my $uri = shift;
	my $last = pop;
	return http( join '', (map {
		my $body = $_;
		"GET $uri HTTP/1.1" . CRLF
		. "Host: localhost" . CRLF
		. "Content-Length: " . (length $body) . CRLF . CRLF
		. $body
	} @_),
		"GET $uri HTTP/1.1" . CRLF
		. "Host: localhost" . CRLF
		. "Connection: close" . CRLF
		. "Content-Length: " . (length $last) . CRLF . CRLF
		. $last
	);
}

sub get_chunked {
	my $uri = shift;
	my $last = pop;
	return http( join '', (map {
		my $body = $_;
		"GET $uri HTTP/1.1" . CRLF
		. "Host: localhost" . CRLF
		. "Transfer-Encoding: chunked" . CRLF . CRLF
		. sprintf("%x", length $body) . CRLF
		. $body . CRLF
		. "0" . CRLF . CRLF
	} @_),
		"GET $uri HTTP/1.1" . CRLF
		. "Host: localhost" . CRLF
		. "Connection: close" . CRLF
		. "Transfer-Encoding: chunked" . CRLF . CRLF
		. sprintf("%x", length $last) . CRLF
		. $last . CRLF
		. "0" . CRLF . CRLF
	);
}

###############################################################################
