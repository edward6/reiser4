#! /bin/sh

LOGFILE=/dev/null
CONTEXT=''

function getfrom() 
{
    local dir
    local file

    dir=$1
    set $(ls -A $dir) NOSUCHFILE
    file=$1
    if [ x$file = xNOSUCHFILE ]
    then
	filefound=''
    else
	filefound=$file
    fi
}

function logit()
{
    echo $CONTEXT $(date) $* >> $LOGFILE
}

function output()
{
    echo $* 
    logit $*
}

function abort()
{
    output $*. Aborting.
    exit 1
}

function die()
{
    abort Command failed.
}

function do_it()
{
    output Executing $*
    $*
}

function systemrestart()
{
    do_it lilo -R $*                                                  || die
    output Bootstring is set to $*
    do_it shutdown -r now                                             || die
}

function do_mkfs()
{
    local fstype
    local device
    local opts

    fstype=$1
    device=$2
    opts=$3

    output Creating $fstype file system on $device with options '"'$opts'"'
    do_it /sbin/mkfs.$fstype $opts $device
}

function do_mount()
{
    local fstype
    local device
    local mpoint
    local mopts

    fstype=$1
    device=$2
    mpoint=$3
    opts=$4

    output Mounting $fstype from $device on $mpoint with '"'$opts'"'
    do_it mount -t$fstype $opts $device $mpoint
}

function reportheader()
{
    local rep

    rep=$1
    echo Started at   >  $rep
    date              >> $rep
    echo -------------------------------------------------- >> $rep
    echo Environment: >> $rep
    uname -a          >> $rep
    echo -------------------------------------------------- >> $rep
    echo CPU          >> $rep
    cat /proc/cpuinfo >> $rep
    echo -------------------------------------------------- >> $rep
    echo Memory       >> $rep
    cat /proc/meminfo >> $rep
    echo -------------------------------------------------- >> $rep
}

function reportfooter()
{
    local rep

    rep=$1
    echo -------------------------------------------------- >> $rep
    echo Finished at  >> $rep
    date              >> $rep
    echo -------------------------------------------------- >> $rep
}
