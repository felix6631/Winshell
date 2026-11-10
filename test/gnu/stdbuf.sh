# GNU comparison of stdbuf: the modes, the errors and the status; the command is run as it is on both sides.
for m in "" -iL -oL "-i 0" "-o 0" "-e 0" "-o 1" "-o 10K" "-o 1KiB" "-o 1KB" "-o 5MB" "-o 5M" "-o 5k" "-o 1G" "-o 1TiB" "-o 1PB" \
         "-o x" "-o 1x" "-o 5b" "-o 1.5" "-o -1" "-o 0x10" "-o ''" "-o 99999999999999999999999" "-o 1YiB" "-o 1ZB" "-i L"; do
    case "$m" in
        "") ;;
        *) compare_words stdbuf "$m echo hi" ;;
    esac
done
compare_words stdbuf "-o L"
compare_words stdbuf ""
compare_words stdbuf "echo hi"
compare_words stdbuf "-i"
compare_words stdbuf "-oL -- echo hi"
compare_words stdbuf "-oL echo -n x"
compare_words stdbuf "-oL --output=0 echo hi"
compare_words stdbuf "-o L nosuchcmd_zz"
compare_words stdbuf "-o L false"
compare_words stdbuf "--output=L echo hi"
compare_words stdbuf "--bogus"
compare_words stdbuf "-x echo hi"
add_case status C - stdbuf --help
add_case status C - stdbuf --version
