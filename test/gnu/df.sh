# GNU comparison of df: each case runs both tools on the drive C: (/c) and compares the table with the digits of the data
# lines replaced by 9: the header, the widths of the columns and the messages have to be the same, the numbers are those
# of the moment. The mount point "/" and the file systems of MSYS are not compared.
df_run() {
    local who=$1 rc
    shift
    MSYS2_ARG_CONV_EXCL="*" "$who" df "$@" > df_out.txt 2> df_err.txt; rc=$?
    awk 'NR == 1 { print; next } { gsub(/[0-9]/, "9"); print }' df_out.txt
    echo '-- err'
    tr -d '\r' < df_err.txt | sed -e 's#^[^ :]*df:#df:#' -e "s#^Try '[^ ]*df #Try 'df #"
    echo "-- rc $rc"
}
df_case() {
    local g o
    total=$((total + 1))
    g=$(df_run gnu "$@")
    o=$(df_run ours "$@")
    if [ "$g" != "$o" ]; then
        bad=$((bad + 1))
        echo "DIFFERS: df $*"
    fi
}
df_case /c
df_case -k /c
df_case -P /c
df_case -h /c
df_case -H /c
df_case -T /c
df_case -i /c
df_case -iT /c
df_case -iP /c
df_case -iH /c
df_case -hT /c
df_case -PT /c
df_case -a /c
df_case -l /c
df_case --local /c
df_case --all /c
df_case --human-readable /c
df_case --si /c
df_case --inodes /c
df_case --print-type /c
df_case --portability /c
df_case --block-size=1M /c
df_case --total /c
df_case --total -h /c
df_case --total -i /c
df_case --total -T /c
df_case --total -P /c
df_case --total /c /c
df_case /c /c
df_case -h /c /c
df_case --sync /c
df_case --no-sync /c
df_case -v /c
for b in 1 3 7 100 512 1000 1023 1024 1025 1500 2000 4096 1000000 1048576 1048577 5000000 12345678 1K 1KB 1KiB 2K 1M 1MB 1MiB 1G 1GB 1T 1P K M G KB MB 18446744073709551615; do
    df_case -B"$b" /c
    df_case -B"$b" -P /c
done
for b in 0 -1 x 1x "1 K" 1e3 1.5K 99999999999999999999 '' KiB B 1b 1Q; do
    df_case -B "$b" /c
done
df_case -hk /c
df_case -kh /c
df_case -k -B1M /c
df_case -B1M -k /c
df_case -h -H /c
df_case -H -h /c
df_case -hP /c
df_case -Ph /c
df_case -kP /c
df_case -B1M -h /c
df_case -h -B1M /c
for t in ntfs NTFS fat32 nosuch; do
    df_case -t $t /c
    df_case -x $t /c
    df_case --type=$t /c
    df_case --exclude-type=$t /c
    df_case -t $t -T /c
done
df_case -t ntfs -t fat32 /c
df_case -t ntfs -x ntfs /c
df_case -x ntfs -x fat32 /c
df_case --output /c
df_case --output=source /c
df_case --output=source,size,used,avail,pcent,target /c
df_case --output=target,source /c
df_case --output=file /c
df_case --output=fstype,file /c
df_case --output=itotal,iused,iavail,ipcent /c
df_case --output=size /c
df_case --output=pcent,used /c
df_case --output=pcent,used --total /c
df_case --output=source,target --total /c
df_case --output=size,used,avail -h /c
df_case --output=size,used,avail -H /c
df_case --output=size -B1M /c
df_case --output=size -B1KB /c
df_case --output=size -k /c
df_case --output=avail /c
df_case --output=avail -h /c
df_case --output=size,size /c
df_case --output=bogus /c
df_case --output=size, /c
df_case --output= /c
df_case --output=,size /c
df_case --output=SIZE /c
df_case --output=source,,size /c
df_case --output=size,pcent,used,avail,target,source,file,fstype /c
df_case -i --output /c
df_case -P --output /c
df_case -T --output /c
df_case -T --output=size /c
df_case --output=size -iP /c
df_case /c nosuchfile
df_case nosuchfile
df_case nosuchfile /c
df_case -h nosuchfile /c
df_case --total nosuchfile /c
df_case -t nosuch nosuchfile
df_case /c/
df_case /c/Users
df_case /c/Windows
df_case -T /c/Windows /c
df_case --output -P /c
add_case status C - df --help
add_case status C - df --version
add_case status C - df -B
add_case status C - df --bogus /c
add_case status C - df -Z /c
