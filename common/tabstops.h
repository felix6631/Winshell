#ifndef TABSTOPS_H
#define TABSTOPS_H

/*
 * The tab stops of expand and unexpand: -t N (a stop every N columns, 8 by default), -t 4,8,12 (the stops
 * listed; after the last one a tab is a single blank), and a last stop written /N (then every N columns from
 * there) or +N (every N columns after the one before).
 */

typedef struct {
    long long* stops;           // the listed stops, ascending
    unsigned long count;
    long long  tabSize;         // a stop every tabSize columns when no list is given
    long long  extend;          // /N: the multiple that goes on after the last stop (0 for none)
    long long  increment;       // +N: the distance that goes on after the last stop (0 for none)
} TabStops;

/** The default: a stop every 8 columns. */
void tabstops_init(TabStops* t);

/**
 * Adds the stops of a -t argument ("4", "4,8,12", "4 8", "8,/4"); further calls add to them. Returns 0, or -1
 * after a message in the name of `program`.
 */
int tabstops_add(const char* program, const char* text, TabStops* t);

/** Ends the list: a single number becomes a tab size. */
void tabstops_finish(TabStops* t);

/** The column of the first tab stop after `column`; column + 1 when there is none (a tab is a blank then). */
long long tabstops_next(const TabStops* t, long long column);

/** Whether the stops are an explicit list or a size that was given (not the default of 8). */
/** Whether there is no tab stop after `column` (a list that has ended); unexpand stops converting there. */
int tabstops_beyond(const TabStops* t, long long column);

void tabstops_free(TabStops* t);

#endif //TABSTOPS_H
