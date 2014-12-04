# spamsink: Dispose of spam, in the cloud.

A memory hole for spam, based on smtp-benchmark by Marc Balmer.

We knew that spammers control a lot of bandwidth, because botnets.  So, for a long time, people thought that fighting spam by just accepting and discarding huge amounts of it would be counterproductive. The spammers get bandwidth for free, and servers are expensive.

There have been some interesting
projects to 
try to waste the spammer's bandwidth by fooling the sender into wasting precious bandwidth and CPU by forcing it to retransmit packets, reorder, and retransmit them.

For example, see [Spam Cannibal](http://www.spamcannibal.org/cannibal.cgi). Other spam tarpits focused on a different part of the spam problem: trapping the address-harvesting part of the spam operation.  Address generators would build a dynamic site which has an infinite hierarchy of random pages, each containing a few bogus email addresses and links to more of these fake pages.  An example is [Sugarplum](http://www.devin.com/sugarplum/) by Devin Carraway.

Sugarplum created pages such as [http://www.devin.com/peaches/lemons](this randomly generated page) and our [directory of people who want to buy herbal weight loss products](http://www.devin.com/peaches/lemons/disentangles/fingerboard) (psst. humans. over here. Not really, just more spamtrap addresses.)  

But in most cases, for a long time, our only hope for fighting spam was to batten down the hatches.  Assume that the spammers control more computing power and bandwidth than we do, and just do the best we can to filter it.


## That was then, this is Cloud

Is that still true?  Except in high-end "fiberhoods," the bandwidth available to end-user machines is stagnant.  Especially upstream bandwidth.  And, much as we like to complain about the security situation on commodity operating systems, it is getting somewhat better.  

And cloud computing continues to be built out everywhere.  Now, lots of people have access to cheap or even free cloud VMs.  If you have extra unused cloud capacity, you can use part of it as a spam sink.

## Do spammers still outnumber us?  Let's find out.

This project is a simple way to build a stand-alone VM that accepts and drops all spam.

Got a free account on a cloud provider, or extra capacity on your hypervisor?   Run a spam sink.  (If you're a cloud provider, you can stick spam sinks on all your spare IP addresses.)


##How to use:

1. Build a spamsink VM with "capstan build" (You can get the Capstan VM-building tool from the [https://github.com/cloudius-systems/capstan](Capstan project on GitHub).

2. Deploy to your favorite private or public cloud.

3. Point some MX records and spamtrap addresses at it.  You can run Sugarplum (above) to generate as many random addresses as you want.


## To try it out locally:

Build and run: `capstan build && capstan run -n bridge`

OSv will print the IP address.  In another terminal, point the smtpsend client at it:

`./smtpsend -n 100`



## TODO: 

Fix the SMTP banner to look like a real mail server.

Save the IP adddresses from which spam comes, and make them available through the OSv API.

Introduce delays.

Modify OSv to drop a fraction of incoming SMTP packets, forcing spammers to consume more bandwidth.

## Sources

**Marc Balmer:** original smtp-benchmark suite, including smtpsink

**Nadav Har'El:** background info on spam tarpits



## Original smtp-benchmark README follows
```
smtp-benchmark version 1.0.0

Copyright (c) 2003, 2004 Marc Balmer.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

   - Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above
     copyright notice, this list of conditions and the following
     disclaimer in the documentation and/or other materials provided
     with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

smtp-benchmark consists of two programs, smtpsend and smtpsink.
Whereas smtpsend is used to send generated e-mail messages using SMTP
to a mail transfer agent, smtpsink is designed to dispose of received
messages as quick as possible.

smtpsend measures the time spent sending e-mails and the number
of e-mails actually sent and outputs statistics after the program
run.

smtpsend can fork one or more parallel senders each using one or more
sequential connections to a SMTP server to deliver one or more
messages per connection.

smtpsink comes in handy when the relaying performance of a MTA is
to be measured.
```


