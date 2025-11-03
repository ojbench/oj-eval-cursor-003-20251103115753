#include <bits/stdc++.h>
using namespace std;

struct SubmissionRec {
    int problemIndex; // 0-based
    string status;    // Accepted, Wrong_Answer, Runtime_Error, Time_Limit_Exceed
    int time;         // submission time
};

struct FrozenInfo {
    bool active = false;                 // whether this problem has become frozen during current freeze
    int preFreezeWrong = 0;              // x in -x/y or 0/y at the moment it first became frozen
    int submissionsCount = 0;            // y in -x/y or 0/y
    vector<pair<string,int>> events;     // (status, time) during freeze
};

struct ProblemState {
    int wrongBeforeAC = 0; // wrong attempts before first AC (applied to effective state)
    bool solved = false;
    int solvedTime = 0;    // time of first AC

    FrozenInfo frozen;     // tracked only when global frozen is true
};

struct Team {
    string name;
    vector<ProblemState> problems; // size M
    vector<SubmissionRec> submissions; // all submissions for QUERY

    // Derived statistics for ranking (considering only applied solves, ignoring frozen.pending)
    int solvedCount = 0;
    int totalPenalty = 0;
    vector<int> solveTimesDesc; // sorted descending among solved problems
};

struct ICPCSystem {
    bool started = false;
    bool isFrozen = false;
    int duration = 0;
    int problemCount = 0; // M <= 26

    vector<Team> teams; // stable index order for storage
    unordered_map<string,int> nameToIndex; // team name -> index

    // Ranking snapshots
    vector<int> currentOrder; // indices of teams in current visible order
    vector<int> lastFlushedOrder; // snapshot after last FLUSH
    unordered_map<string,int> lastFlushedRank; // team name -> ranking number (1-based)

    // Helpers
    static bool isNonAC(const string &s) {
        return s == "Wrong_Answer" || s == "Runtime_Error" || s == "Time_Limit_Exceed";
    }

    void addTeam(const string &teamName) {
        if (started) {
            cout << "[Error]Add failed: competition has started.\n";
            return;
        }
        if (nameToIndex.count(teamName)) {
            cout << "[Error]Add failed: duplicated team name.\n";
            return;
        }
        Team t; t.name = teamName; t.problems.clear();
        // problems will be sized when START begins
        teams.push_back(move(t));
        nameToIndex[teamName] = (int)teams.size() - 1;
        cout << "[Info]Add successfully.\n";
    }

    void start(int dur, int m) {
        if (started) {
            cout << "[Error]Start failed: competition has started.\n";
            return;
        }
        started = true; duration = dur; problemCount = m;
        for (auto &t : teams) {
            t.problems.assign(problemCount, ProblemState());
            t.solvedCount = 0; t.totalPenalty = 0; t.solveTimesDesc.clear();
        }
        // Initial ranking by team name lex order
        currentOrder.resize(teams.size());
        iota(currentOrder.begin(), currentOrder.end(), 0);
        sort(currentOrder.begin(), currentOrder.end(), [&](int a, int b){
            return teams[a].name < teams[b].name;
        });
        // Before first FLUSH, rankings are based on name lexicographic order
        lastFlushedOrder = currentOrder;
        lastFlushedRank.clear();
        for (size_t i = 0; i < lastFlushedOrder.size(); ++i) {
            lastFlushedRank[teams[lastFlushedOrder[i]].name] = (int)i + 1;
        }
        cout << "[Info]Competition starts.\n";
    }

    // Recompute derived stats for one team from its problem states (excluding still-frozen unapplied events)
    void recomputeTeamStats(int ti) {
        Team &t = teams[ti];
        int solved = 0; long long penalty = 0;
        vector<int> times;
        times.reserve(problemCount);
        for (int i = 0; i < problemCount; ++i) {
            const auto &p = t.problems[i];
            if (p.solved) {
                solved++;
                penalty += 20LL * p.wrongBeforeAC + p.solvedTime;
                times.push_back(p.solvedTime);
            }
        }
        sort(times.begin(), times.end(), greater<int>());
        t.solvedCount = solved;
        t.totalPenalty = (int)penalty;
        t.solveTimesDesc = move(times);
    }

    // Comparator according to rules; assumes both teams have up-to-date stats
    bool betterTeam(int a, int b) {
        const Team &A = teams[a];
        const Team &B = teams[b];
        if (A.solvedCount != B.solvedCount) return A.solvedCount > B.solvedCount;
        if (A.totalPenalty != B.totalPenalty) return A.totalPenalty < B.totalPenalty;
        // Compare solve times: smaller maximum better; times are sorted descending
        size_t k = A.solveTimesDesc.size(); // equals B's size here
        for (size_t i = 0; i < k; ++i) {
            if (A.solveTimesDesc[i] != B.solveTimesDesc[i]) return A.solveTimesDesc[i] < B.solveTimesDesc[i];
        }
        return A.name < B.name;
    }

    void submit(const string &probName, const string &teamName, const string &status, int time) {
        int ti = nameToIndex[teamName];
        int pi = probName[0] - 'A';
        Team &t = teams[ti];
        ProblemState &p = t.problems[pi];
        // record for query
        t.submissions.push_back({pi, status, time});

        if (!isFrozen) {
            // normal: apply immediately
            if (!p.solved) {
                if (status == "Accepted") {
                    p.solved = true; p.solvedTime = time;
                    // incrementally update team stats
                    t.solvedCount += 1;
                    t.totalPenalty += 20 * p.wrongBeforeAC + p.solvedTime;
                    // insert solved time into descending vector
                    auto &vt = t.solveTimesDesc;
                    auto it = lower_bound(vt.begin(), vt.end(), p.solvedTime, greater<int>());
                    vt.insert(it, p.solvedTime);
                } else if (isNonAC(status)) {
                    p.wrongBeforeAC++;
                }
                // Update team stats lazily when needed
            }
        } else {
            // During freeze: only problems unsolved before freeze may become frozen upon submission
            if (!p.solved) {
                if (!p.frozen.active) {
                    p.frozen.active = true;
                    p.frozen.preFreezeWrong = p.wrongBeforeAC; // snapshot x at first frozen submission
                }
                p.frozen.submissionsCount++;
                p.frozen.events.emplace_back(status, time);
                // Do NOT apply effects now
            } else {
                // Already solved before freeze: ignore for scoreboard purposes
            }
        }
    }

    void doFlush(bool announce = true) {
        // Stats are maintained incrementally; just sort current visible order
        // Sort current visible order
        currentOrder.resize(teams.size());
        iota(currentOrder.begin(), currentOrder.end(), 0);
        sort(currentOrder.begin(), currentOrder.end(), [&](int a, int b){ return betterTeam(a,b); });
        // Update last flushed snapshot
        lastFlushedOrder = currentOrder;
        lastFlushedRank.clear();
        for (size_t i = 0; i < lastFlushedOrder.size(); ++i) {
            lastFlushedRank[teams[lastFlushedOrder[i]].name] = (int)i + 1;
        }
        if (announce) cout << "[Info]Flush scoreboard.\n";
    }

    // Recompute visible order without touching lastFlushed snapshot
    void recomputeVisibleOrderNoSnapshot() {
        for (size_t i = 0; i < teams.size(); ++i) recomputeTeamStats((int)i);
        currentOrder.resize(teams.size());
        iota(currentOrder.begin(), currentOrder.end(), 0);
        sort(currentOrder.begin(), currentOrder.end(), [&](int a, int b){ return betterTeam(a,b); });
    }

    void freeze() {
        if (isFrozen) {
            cout << "[Error]Freeze failed: scoreboard has been frozen.\n";
            return;
        }
        isFrozen = true;
        cout << "[Info]Freeze scoreboard.\n";
    }

    // Helper to render a single problem cell
    string renderProblemCell(const Team &t, const ProblemState &p) const {
        if (!isFrozen || !p.frozen.active) {
            if (p.solved) {
                if (p.wrongBeforeAC == 0) return "+";
                return string("+") + to_string(p.wrongBeforeAC);
            } else {
                if (p.wrongBeforeAC == 0) return ".";
                return string("-") + to_string(p.wrongBeforeAC);
            }
        } else {
            // frozen display: only for problems that entered frozen state (active)
            int x = p.frozen.preFreezeWrong;
            int y = p.frozen.submissionsCount;
            if (x == 0) return to_string(0) + "/" + to_string(y);
            return string("-") + to_string(x) + "/" + to_string(y);
        }
    }

    void printScoreboard(const vector<int> &order) {
        // Ranking numbers are based on 'order' itself
        for (size_t i = 0; i < order.size(); ++i) {
            const Team &t = teams[order[i]];
            // Ensure stats up to date for visible state
            // recomputeTeamStats(order[i]); // already computed before ordering
            cout << t.name << ' ' << (i+1) << ' ' << t.solvedCount << ' ' << t.totalPenalty;
            for (int p = 0; p < problemCount; ++p) {
                cout << ' ' << renderProblemCell(t, t.problems[p]);
            }
            cout << "\n";
        }
    }

    // Apply frozen events for a single problem and clear its frozen state
    void unfreezeProblemApply(int ti, int pi) {
        ProblemState &p = teams[ti].problems[pi];
        if (!p.frozen.active) return;
        for (auto &ev : p.frozen.events) {
            const string &status = ev.first; int time = ev.second;
            if (p.solved) continue; // ignore after solved
            if (status == "Accepted") {
                p.solved = true; p.solvedTime = time;
            } else if (isNonAC(status)) {
                p.wrongBeforeAC++;
            }
        }
        // clear frozen
        p.frozen.active = false;
        p.frozen.preFreezeWrong = 0;
        p.frozen.submissionsCount = 0;
        p.frozen.events.clear();
    }

    bool teamHasFrozen(int ti) const {
        const Team &t = teams[ti];
        for (int i = 0; i < problemCount; ++i) if (t.problems[i].frozen.active) return true;
        return false;
    }

    // Find lowest-ranked team with frozen problems according to 'order'; return index in 'order' or -1
    int findLowestTeamWithFrozen(const vector<int> &order) const {
        for (int i = (int)order.size()-1; i >= 0; --i) {
            if (teamHasFrozen(order[i])) return i;
        }
        return -1;
    }

    // Among frozen problems of team ti, choose smallest problem index
    int chooseSmallestFrozenProblem(int ti) const {
        for (int i = 0; i < problemCount; ++i) if (teams[ti].problems[i].frozen.active) return i;
        return -1;
    }

    void scroll() {
        if (!isFrozen) {
            cout << "[Error]Scroll failed: scoreboard has not been frozen.\n";
            return;
        }
        cout << "[Info]Scroll scoreboard.\n";
        // First, recompute the visible order like a flush, but do NOT update lastFlushed snapshot
        recomputeVisibleOrderNoSnapshot();
        // Output the scoreboard before scrolling (after flush)
        printScoreboard(currentOrder);

        // We'll update currentOrder incrementally as we unfreeze
        // Maintain positions and an ordered set to fetch the lowest-ranked team with frozen problems
        vector<int> posOfTeam(teams.size());
        for (size_t i = 0; i < currentOrder.size(); ++i) posOfTeam[currentOrder[i]] = (int)i;
        auto hasFrozen = [&](int ti)->bool{ return teamHasFrozen(ti); };
        struct KeyCmp {
            const vector<int>* posPtr;
            bool operator()(const pair<int,int>& a, const pair<int,int>& b) const {
                if (a.first != b.first) return a.first < b.first; // order by position
                return a.second < b.second; // tie by team index for stability
            }
        };
        KeyCmp cmp; cmp.posPtr = &posOfTeam; // not used directly but kept for signature symmetry
        set<pair<int,int>, KeyCmp> S(cmp);
        for (size_t i = 0; i < teams.size(); ++i) if (hasFrozen((int)i)) S.insert({posOfTeam[i], (int)i});

        while (!S.empty()) {
            auto itMax = prev(S.end());
            int ti = itMax->second;
            int curPos = itMax->first;
            // choose smallest frozen problem of this team
            int pi = chooseSmallestFrozenProblem(ti);
            if (pi < 0) { continue; }
            // Apply this unfreeze
            unfreezeProblemApply(ti, pi);
            // Update this team's stats only
            recomputeTeamStats(ti);
            // Bubble up if needed; output a single line if ranking changed
            string lastOpponent;
            int pos = curPos;
            // The team ti may be in set; we'll erase and re-insert with updated positions as needed
            // Remove current ti entry; we'll re-add if still has frozen after bubbling
            S.erase({curPos, ti});
            while (pos > 0) {
                int tj = currentOrder[pos-1];
                if (betterTeam(ti, tj)) {
                    lastOpponent = teams[tj].name;
                    // swap positions
                    currentOrder[pos-1] = ti;
                    currentOrder[pos] = tj;
                    int oldPosTi = pos;
                    int oldPosTj = pos-1;
                    posOfTeam[ti] = oldPosTj;
                    posOfTeam[tj] = oldPosTi;
                    // Update set entries for tj if it has frozen problems
                    if (hasFrozen(tj)) {
                        // Erase old and insert new
                        S.erase({oldPosTj, tj});
                        S.insert({posOfTeam[tj], tj});
                    }
                    pos--;
                } else {
                    break;
                }
            }
            if (!lastOpponent.empty()) {
                cout << teams[ti].name << ' ' << lastOpponent << ' ' << teams[ti].solvedCount << ' ' << teams[ti].totalPenalty << "\n";
            }
            // If still has frozen problems, reinsert with updated position
            if (hasFrozen(ti)) {
                S.insert({posOfTeam[ti], ti});
            }
        }

        // After done, output the final scoreboard after scrolling
        printScoreboard(currentOrder);

        // Clear global frozen state
        isFrozen = false;
        // After scrolling ends, the scoreboard is up to date; update last flushed snapshot to reflect it
        lastFlushedOrder = currentOrder;
        lastFlushedRank.clear();
        for (size_t i = 0; i < lastFlushedOrder.size(); ++i) {
            lastFlushedRank[teams[lastFlushedOrder[i]].name] = (int)i + 1;
        }
        // Ensure any remaining per-problem frozen flags are cleared (there shouldn't be any)
        for (auto &t : teams) {
            for (auto &p : t.problems) {
                if (p.frozen.active) {
                    // Problems that never received frozen submissions remain inactive; active implies y>0
                    // But if something remains, clear it (defensive)
                    p.frozen.active = false;
                    p.frozen.preFreezeWrong = 0;
                    p.frozen.submissionsCount = 0;
                    p.frozen.events.clear();
                }
            }
        }
    }

    void queryRanking(const string &teamName) {
        auto it = nameToIndex.find(teamName);
        if (it == nameToIndex.end()) {
            cout << "[Error]Query ranking failed: cannot find the team.\n";
            return;
        }
        cout << "[Info]Complete query ranking.\n";
        if (isFrozen) {
            cout << "[Warning]Scoreboard is frozen. The ranking may be inaccurate until it were scrolled.\n";
        }
        int rk = lastFlushedRank.count(teamName) ? lastFlushedRank[teamName] : 0;
        cout << teamName << " NOW AT RANKING " << rk << "\n";
    }

    void querySubmission(const string &teamName, const string &probFilter, const string &statusFilter) {
        auto it = nameToIndex.find(teamName);
        if (it == nameToIndex.end()) {
            cout << "[Error]Query submission failed: cannot find the team.\n";
            return;
        }
        int ti = it->second;
        const Team &t = teams[ti];
        cout << "[Info]Complete query submission.\n";
        int lastIdx = -1;
        for (int i = (int)t.submissions.size()-1; i >= 0; --i) {
            const auto &s = t.submissions[i];
            bool probOk = (probFilter == "ALL") || (s.problemIndex == (probFilter[0]-'A'));
            bool statusOk = (statusFilter == "ALL") || (s.status == statusFilter);
            if (probOk && statusOk) { lastIdx = i; break; }
        }
        if (lastIdx < 0) {
            cout << "Cannot find any submission.\n";
        } else {
            const auto &s = t.submissions[lastIdx];
            char probChar = char('A' + s.problemIndex);
            cout << t.name << ' ' << probChar << ' ' << s.status << ' ' << s.time << "\n";
        }
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    ICPCSystem sys;

    string cmd;
    while ( (cin >> cmd) ) {
        if (cmd == "ADDTEAM") {
            string team; cin >> team;
            sys.addTeam(team);
        } else if (cmd == "START") {
            string kw1, kw2; int D, P; // DURATION [D] PROBLEM [P]
            cin >> kw1 >> D >> kw2 >> P;
            sys.start(D, P);
        } else if (cmd == "SUBMIT") {
            string prob, kw1, team, kw2, status, kw3; int t;
            // SUBMIT [problem_name] BY [team_name] WITH [submit_status] AT [time]
            cin >> prob >> kw1 >> team >> kw2 >> status >> kw3 >> t;
            sys.submit(prob, team, status, t);
        } else if (cmd == "FLUSH") {
            sys.doFlush(true);
        } else if (cmd == "FREEZE") {
            sys.freeze();
        } else if (cmd == "SCROLL") {
            sys.scroll();
        } else if (cmd == "QUERY_RANKING") {
            string team; cin >> team;
            sys.queryRanking(team);
        } else if (cmd == "QUERY_SUBMISSION") {
            string team, kw1, probEq, kw2, statusEq;
            // QUERY_SUBMISSION [team_name] WHERE PROBLEM=[problem_name] AND STATUS=[status]
            cin >> team >> kw1 >> probEq >> kw2 >> statusEq;
            string probName = probEq.substr(strlen("PROBLEM="));
            string statusName = statusEq.substr(strlen("STATUS="));
            sys.querySubmission(team, probName, statusName);
        } else if (cmd == "END") {
            cout << "[Info]Competition ends.\n";
            break;
        } else {
            // Unknown command (not expected by spec)
        }
    }
    return 0;
}
