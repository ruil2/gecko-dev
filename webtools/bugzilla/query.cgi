#!/usr/bonsaitools/bin/perl -wT
# -*- Mode: perl; indent-tabs-mode: nil -*-
#
# The contents of this file are subject to the Mozilla Public
# License Version 1.1 (the "License"); you may not use this file
# except in compliance with the License. You may obtain a copy of
# the License at http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS
# IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
# implied. See the License for the specific language governing
# rights and limitations under the License.
#
# The Original Code is the Bugzilla Bug Tracking System.
#
# The Initial Developer of the Original Code is Netscape Communications
# Corporation. Portions created by Netscape are
# Copyright (C) 1998 Netscape Communications Corporation. All
# Rights Reserved.
#
# Contributor(s): Terry Weissman <terry@mozilla.org>
#                 David Gardiner <david.gardiner@unisa.edu.au>
#                 Matthias Radestock <matthias@sorted.org>
#                 Gervase Markham <gerv@gerv.net>

use diagnostics;
use strict;
use lib ".";

require "CGI.pl";

use vars qw(
    @CheckOptionValues
    @legal_resolution
    @legal_bug_status
    @legal_components
    @legal_keywords
    @legal_opsys
    @legal_platform
    @legal_priority
    @legal_product
    @legal_severity
    @legal_target_milestone
    @legal_versions
    @log_columns
    %versions
    %components
    %FORM
    $template
    $vars
);

ConnectToDatabase();

if (defined $::FORM{"GoAheadAndLogIn"}) {
    # We got here from a login page, probably from relogin.cgi.  We better
    # make sure the password is legit.
    confirm_login();
} else {
    quietly_check_login();
}

# Backwards compatibility hack -- if there are any of the old QUERY_*
# cookies around, and we are logged in, then move them into the database
# and nuke the cookie. This is required for Bugzilla 2.8 and earlier.
if ($::userid) {
    my @oldquerycookies;
    foreach my $i (keys %::COOKIE) {
        if ($i =~ /^QUERY_(.*)$/) {
            push(@oldquerycookies, [$1, $i, $::COOKIE{$i}]);
        }
    }
    if (defined $::COOKIE{'DEFAULTQUERY'}) {
        push(@oldquerycookies, [$::defaultqueryname, 'DEFAULTQUERY',
                                $::COOKIE{'DEFAULTQUERY'}]);
    }
    if (@oldquerycookies) {
        foreach my $ref (@oldquerycookies) {
            my ($name, $cookiename, $value) = (@$ref);
            if ($value) {
                my $qname = SqlQuote($name);
                SendSQL("SELECT query FROM namedqueries " .
                        "WHERE userid = $::userid AND name = $qname");
                my $query = FetchOneColumn();
                if (!$query) {
                    SendSQL("REPLACE INTO namedqueries " .
                            "(userid, name, query) VALUES " .
                            "($::userid, $qname, " . SqlQuote($value) . ")");
                }
            }
            print "Set-Cookie: $cookiename= ; path=" . Param("cookiepath") . 
                  "; expires=Sun, 30-Jun-1980 00:00:00 GMT\n";
        }
    }
}

if ($::FORM{'nukedefaultquery'}) {
    if ($::userid) {
        SendSQL("DELETE FROM namedqueries " .
                "WHERE userid = $::userid AND name = '$::defaultqueryname'");
    }
    $::buffer = "";
}

my $userdefaultquery;
if ($::userid) {
    SendSQL("SELECT query FROM namedqueries " .
            "WHERE userid = $::userid AND name = '$::defaultqueryname'");
    $userdefaultquery = FetchOneColumn();
}

my %default;

# We pass the defaults as a hash of references to arrays. For those
# Items which are single-valued, the template should only reference [0]
# and ignore any multiple values.
sub PrefillForm {
    my ($buf) = (@_);
    my $foundone = 0;

    # Nothing must be undef, otherwise the template complains.
    foreach my $name ("bug_status", "resolution", "assigned_to",
                      "rep_platform", "priority", "bug_severity",
                      "product", "reporter", "op_sys",
                      "component", "version", "chfield", "chfieldfrom",
                      "chfieldto", "chfieldvalue", "target_milestone",
                      "email", "emailtype", "emailreporter",
                      "emailassigned_to", "emailcc", "emailqa_contact",
                      "emaillongdesc",
                      "changedin", "votes", "short_desc", "short_desc_type",
                      "long_desc", "long_desc_type", "bug_file_loc",
                      "bug_file_loc_type", "status_whiteboard",
                      "status_whiteboard_type", "bug_id",
                      "bugidtype", "keywords", "keywords_type") {
        # This is a bit of a hack. The default, empty list has 
        # three entries to accommodate the needs of the email fields -
        # we use each position to denote the relevant field. Array
        # position 0 is unused for email fields because the form 
        # parameters historically started at 1.
        $default{$name} = ["", "", ""];
    }
 
 
    # Iterate over the URL parameters
    foreach my $item (split(/\&/, $buf)) {
        my @el = split(/=/, $item);
        my $name = $el[0];
        my $value;
        if ($#el > 0) {
            $value = url_decode($el[1]);
        } else {
            $value = "";
        }
        
        # If the name ends in a number (which it does for the fields which
        # are part of the email searching), we use the array
        # positions to show the defaults for that number field.
        if ($name =~ m/^(.+)(\d)$/ && defined($default{$1})) {
            $foundone = 1;
            $default{$1}->[$2] = $value;
        }
        # If there's no default yet, we replace the blank string.
        elsif (defined($default{$name}) && $default{$name}->[0] eq "") {
            $foundone = 1;
            $default{$name} = [$value]; 
        } 
        # If there's already a default, we push on the new value.
        elsif (defined($default{$name})) {
            push (@{$default{$name}}, $value);
        }        
    }        
    return $foundone;
}


if (!PrefillForm($::buffer)) {
    # Ah-hah, there was no form stuff specified.  Do it again with the
    # default query.
    if ($userdefaultquery) {
        PrefillForm($userdefaultquery);
    } else {
        PrefillForm(Param("defaultquery"));
    }
}

if ($default{'chfieldto'}->[0] eq "") {
    $default{'chfieldto'} = ["Now"];
}

GetVersionTable();

# if using usebuggroups, then we don't want people to see products they don't
# have access to. Remove them from the list.

my @products = ();
my %component_set;
my %version_set;
my %milestone_set;
foreach my $p (@::legal_product) {
    # If we're using bug groups to restrict entry on products, and
    # this product has a bug group, and the user is not in that
    # group, we don't want to include that product in this list.
    next if (Param("usebuggroups") && GroupExists($p) && !UserInGroup($p));

    # We build up boolean hashes in the "-set" hashes for each of these things 
    # before making a list because there may be duplicates names across products.
    push @products, $p;
    if ($::components{$p}) {
        foreach my $c (@{$::components{$p}}) {
            $component_set{$c} = 1;
        }
    }
    foreach my $v (@{$::versions{$p}}) {
        $version_set{$v} = 1;
    }
    foreach my $m (@{$::target_milestone{$p}}) {
        $milestone_set{$m} = 1;
    }
}

# @products is now all the products we are ever concerned with, as a list
# %x_set is now a unique "list" of the relevant components/versions/tms
@products = sort { lc($a) cmp lc($b) } @products;

# Create the component, version and milestone lists.
my @components = ();
my @versions = ();
my @milestones = ();
foreach my $c (@::legal_components) {
    if ($component_set{$c}) {
        push @components, $c;
    }
}
foreach my $v (@::legal_versions) {
    if ($version_set{$v}) {
        push @versions, $v;
    }
}
foreach my $m (@::legal_target_milestone) {
    if ($milestone_set{$m}) {
        push @milestones, $m;
    }
}

# Sort the component list...
my $comps = \%::components;
foreach my $p (@products) {
    my @tmp = sort { lc($a) cmp lc($b) } @{$comps->{$p}};
    $comps->{$p} = \@tmp;
}    

# and the version list...
my $vers = \%::versions;
foreach my $p (@products) {
    my @tmp = sort { lc($a) cmp lc($b) } @{$vers->{$p}};
    $vers->{$p} = \@tmp;
}    

# and the milestone list.
my $mstones;
if (Param('usetargetmilestone')) {
    $mstones = \%::target_milestone;
    foreach my $p (@products) {
        my @tmp = sort { lc($a) cmp lc($b) } @{$mstones->{$p}};
        $mstones->{$p} = \@tmp;
    }    
}

# "foo" or "foos" is a list of all the possible (or legal) products, 
# components, versions or target milestones.
# "foobyproduct" is a hash, keyed by product, of sorted lists
# of the same data.

$vars->{'product'} = \@products;

# We use 'component_' because 'component' is a Template Toolkit reserved word.
$vars->{'componentsbyproduct'} = $comps;
$vars->{'component_'} = \@components;

$vars->{'versionsbyproduct'} = $vers;
$vars->{'version'} = \@versions;

if (Param('usetargetmilestone')) {
    $vars->{'milestonesbyproduct'} = $mstones;
    $vars->{'target_milestone'} = \@milestones;
}

$vars->{'have_keywords'} = scalar(@::legal_keywords);

push @::legal_resolution, "---"; # Oy, what a hack.
shift @::legal_resolution; 
      # Another hack - this array contains "" for some reason. See bug 106589.
$vars->{'resolution'} = \@::legal_resolution;

$vars->{'chfield'} = ["[Bug creation]", @::log_columns];
$vars->{'bug_status'} = \@::legal_bug_status;
$vars->{'rep_platform'} = \@::legal_platform;
$vars->{'op_sys'} = \@::legal_opsys;
$vars->{'priority'} = \@::legal_priority;
$vars->{'bug_severity'} = \@::legal_severity;
$vars->{'userid'} = $::userid;

# Boolean charts
my @fields;
push(@fields, { name => "noop", description => "---" });
SendSQL("SELECT name, description FROM fielddefs ORDER BY sortkey");
while (MoreSQLData()) {
    my ($name, $description) = FetchSQLData();
    push(@fields, { name => $name, description => $description });
}

$vars->{'fields'} = \@fields;

# Creating new charts - if the cmd-add value is there, we define the field
# value so the code sees it and creates the chart. It will attempt to select
# "xyzzy" as the default, and fail. This is the correct behaviour.
foreach my $cmd (grep(/^cmd-/, keys(%::FORM))) {
    if ($cmd =~ /^cmd-add(\d+)-(\d+)-(\d+)$/) {
        $::FORM{"field$1-$2-$3"} = "xyzzy";
    }
}

if (!exists $::FORM{'field0-0-0'}) {
    $::FORM{'field0-0-0'} = "xyzzy";
}

# Create data structure of boolean chart info. It's an array of arrays of
# arrays - with the inner arrays having three members - field, type and
# value.
my @charts;
for (my $chart = 0; $::FORM{"field$chart-0-0"}; $chart++) {
    my @rows;
    for (my $row = 0; $::FORM{"field$chart-$row-0"}; $row++) {
        my @cols;
        for (my $col = 0; $::FORM{"field$chart-$row-$col"}; $col++) {
            push(@cols, { field => $::FORM{"field$chart-$row-$col"},
                          type => $::FORM{"type$chart-$row-$col"},
                          value => $::FORM{"value$chart-$row-$col"} });
        }
        push(@rows, \@cols);
    }
    push(@charts, \@rows);
}

$default{'charts'} = \@charts;

# Named queries
if ($::userid) {
    my @namedqueries;
    SendSQL("SELECT name FROM namedqueries " .
            "WHERE userid = $::userid AND name != '$::defaultqueryname' " .
            "ORDER BY name");
    while (MoreSQLData()) {
        push(@namedqueries, FetchOneColumn());
    }
    
    $vars->{'namedqueries'} = \@namedqueries;    
}

# Sort order
my $deforder;
my @orders = ('Bug Number', 'Importance', 'Assignee', 'Last Changed');

if ($::COOKIE{'LASTORDER'}) {
    $deforder = "Reuse same sort as last time";
    unshift(@orders, $deforder);
}

if ($::FORM{'order'}) { $deforder = $::FORM{'order'} }

$vars->{'userdefaultquery'} = $userdefaultquery;
$vars->{'orders'} = \@orders;
$default{'querytype'} = $deforder || 'Importance';

# Add in the defaults.
$vars->{'default'} = \%default;

# Generate and return the UI (HTML page) from the appropriate template.
print "Content-type: text/html\n\n";
$template->process("search/search.html.tmpl", $vars)
  || ThrowTemplateError($template->error());
