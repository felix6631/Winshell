# GNU comparison of date: sourced by gnu_compare.sh (helpers: compare, compare_stdin, add_case, ...).
# The zone name is left out (%Z, and the default format): MSYS has none without TZ, Windows names it.
DATE_FORMATS=('+%a|%A|%b|%B|%h|%d|%e|%j|%m|%y|%Y|%C' '+%H|%I|%k|%l|%M|%S|%p|%P|%s|%N' '+%u|%w|%U|%W|%V|%g|%G'
    '+%D|%F|%T|%R|%r|%x|%X|%c' '+%z|%:z|%::z|%:::z' '+%-d|%-m|%_d|%_m|%0e|%^a|%^B|%#b|%#p|%10Y|%5d|%-5d|%_5d|%05e'
    '+%3N|%6N|%1N|%%|%t|%n|%q|%' '+%5a|%-5a|%010A|%^10b|%e%e|%-H|%-I|%-j|%_j|%03u' '+%5s|%-s')
DATE_INSTANTS=(0 1 59 86399 86400 951782400 1000000000 1234567890 1609459199 1609459200 1609545600 1704067200 \
               1735603200 1735689599 1782864000 1893456000 4102444799 -1 -86400)
for t in "${DATE_INSTANTS[@]}"; do
    for fmt in "${DATE_FORMATS[@]}"; do
        compare date -d "@$t" "$fmt"
        compare date -u -d "@$t" "$fmt"
    done
done
# far from the years of the C library: UTC only (the rules of summer time are not known there)
for t in -2208988800 -62135596800 253402300799 32535215999 32535216000; do
    for fmt in "${DATE_FORMATS[@]}"; do
        compare date -u -d "@$t" "$fmt"
    done
done
for d in "2026-10-14" "2026-10-14 13:45:10" "2026-10-14 13:45" "2026-10-14T13:45:10" "2026-10-14T13:45:10Z" "2026-10-14T13:45:10+02:00" \
         "2026-10-14T13:45:10-0530" "2020-02-29" "2021-01-03" "2020-12-31 23:59:59" "1999-12-31" "2000-01-01 00:00:00"; do
    compare date -d "$d" '+%F %T %s %z'
    compare date -u -d "$d" '+%F %T %s %z'
done
for d in "Oct 14 2026" "14 Oct 2026" "Oct 14, 2026" "October 14 2026 10:00" "1 Jan 2020" "2026-10-14 +2 days" "2026-10-14 3 weeks ago" "2026-10-31 +1 month" "2026-01-31 1 month" "2024-02-29 next year" \
         "2026-10-14 12:00 2 hours ago" "2026-10-14 next day" "2026-10-14 last day" \
         "2026-10-14 yesterday" "2026-10-14 tomorrow" "2026-10-14 monday" "2026-10-14 next monday" "2026-10-14 last friday" \
         "2026-10-14 wed" "2026-10-14 10:30 pm" "2026-10-14 12:30 am" "@1700000000" "@0" "@-5" "@1.5"; do
    compare date -u -d "$d" '+%F %T'
done
for bad in "2026-13-01" "10:99" "@" "@x" "yesterdayx"; do
    compare date -u -d "$bad" '+%F %T'
done
compare date -u -d @1700000000 -I
compare date -u -d @1700000000 --iso-8601=hours
compare date -u -d @1700000000 --iso-8601=minutes
compare date -u -d @1700000000 --iso-8601=seconds
compare date -u -d @1700000000 --iso-8601=ns
compare date -u -d @1700000000 -Idate
compare date -u -d @1700000000 --iso-8601=bogus
compare date -u -d @1700000000 -R
compare date -u -d @1700000000 --rfc-email
compare date -u -d @1700000000 --rfc-3339=date
compare date -u -d @1700000000 --rfc-3339=seconds
compare date -u -d @1700000000 --rfc-3339=ns
compare date -u -d @1700000000 --rfc-3339=bogus
compare date -d @1700000000 -I
compare date -d @1700000000 -R
compare date -d @1700000000 --rfc-3339=seconds
compare date -d @1700000000 --iso-8601=seconds
compare date -u -d @1 -d @2 '+%s'
compare date -u -d @1 -r words.txt '+%s'
compare date -r nosuch.txt '+%s'
compare date -u '+%s' extra
compare date extra
compare date -u -d @1 '+%s' '+%F'
compare date -u -d @1 -I '+%s'
compare date --bogus
