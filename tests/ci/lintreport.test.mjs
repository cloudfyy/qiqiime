// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

import assert from "node:assert/strict";
import test from "node:test";
import {
  notifyMorningReport, renderReport, reportWindow, selectNightlyRun, trackingMarker,
} from "../../scripts/nightly_lint_report.mjs";

const now = new Date("2026-11-18T00:30:00Z");
const repository = "cloudfyy/qiqiime";
const recipient = "cloudfyy";
const run = {
  id: 123, display_title: "Nightly lint report", head_branch: "main",
  event: "schedule", created_at: "2026-11-17T19:31:00Z",
  head_sha: "a".repeat(40), status: "completed", conclusion: "success",
};
const jobs = ["x64", "x86"].map((arch) => ({
  name: `Lint (${arch})`, status: "completed", conclusion: "success",
}));

test("the morning report uses the same Beijing day as the previous UTC evening run", () => {
  const window = reportWindow(now);
  assert.equal(window.day, "2026-11-18");
  assert.equal(window.start.toISOString(), "2026-11-17T19:30:00.000Z");
  assert.equal(window.midnight.toISOString(), "2026-11-17T16:00:00.000Z");
  assert.equal(reportWindow(new Date("2026-12-31T19:30:00Z")).day, "2027-01-01");
  assert.throws(() => reportWindow("invalid"), /Invalid report time/);
});

test("selection excludes reminders, yesterday, future runs and unrelated branches/events", () => {
  const excluded = [
    { ...run, display_title: "Morning lint report reminder", id: 200 },
    { ...run, created_at: "2026-11-16T19:31:00Z", id: 201 },
    { ...run, created_at: "2026-11-18T01:00:00Z", id: 202 },
    { ...run, head_branch: "feature", id: 203 },
    { ...run, event: "pull_request", id: 204 },
  ];
  assert.equal(selectNightlyRun(excluded, now, "main"), null);
  assert.equal(selectNightlyRun([...excluded, run], now, "main"), run);
  const rerun = { ...run, id: 124, event: "workflow_dispatch", created_at: "2026-11-17T20:00:00Z" };
  assert.equal(selectNightlyRun([run, rerun], now, "main"), rerun);
});

test("only both completed successful architecture jobs certify a pass", () => {
  const body = renderReport({ run, jobs, now, repository, recipient });
  assert.match(body, /\*\*PASS\*\*/);
  assert.match(body, /@cloudfyy/);
  assert.match(body, /actions\/runs\/123/);
  assert.match(body, /14 days/);
  assert.match(body, /not unit-test/);
  for (const conclusion of ["failure", "cancelled", "skipped", "timed_out", "neutral", null]) {
    const changed = [{ ...jobs[0], conclusion }, jobs[1]];
    const result = renderReport({ run, jobs: changed, now, repository, recipient });
    assert.doesNotMatch(result, /\*\*PASS\*\*/);
    assert.match(result, /NOT A PASS/);
  }
  for (const changed of [[], [jobs[0]], [...jobs, jobs[0]], [{ ...jobs[0], status: "in_progress" }, jobs[1]]]) {
    assert.doesNotMatch(renderReport({ run, jobs: changed, now, repository, recipient }), /\*\*PASS\*\*/);
  }
});

test("missing, queued, running and failed runs never borrow yesterday's success", () => {
  assert.match(renderReport({ run: null, now, repository, recipient }), /NO RUN FOUND/);
  for (const status of ["queued", "in_progress"]) {
    assert.match(renderReport({ run: { ...run, status }, jobs: [], now, repository, recipient }), /IN PROGRESS/);
  }
  assert.match(renderReport({
    run: { ...run, conclusion: "failure" }, jobs, now, repository, recipient,
  }), /FAILED OR NOT SUCCESSFUL/);
  assert.throws(() => renderReport({
    run: { ...run, head_sha: "unknown" }, jobs, now, repository, recipient,
  }), /Invalid nightly run identity/);
});

function fixture({ runs = [run], comments = [], issue = { state: "open", body: trackingMarker } } = {}) {
  const calls = [];
  const rest = {
    actions: { listWorkflowRuns: Symbol("runs"), listJobsForWorkflowRun: Symbol("jobs") },
    issues: {
      listComments: Symbol("comments"),
      get: async () => ({ data: issue }),
      createComment: async (value) => calls.push({ type: "create", ...value }),
      updateComment: async (value) => calls.push({ type: "update", ...value }),
    },
  };
  const github = {
    rest,
    paginate: async (method, options) => {
      calls.push({ type: "read", method, options });
      if (method === rest.actions.listWorkflowRuns) return runs;
      if (method === rest.actions.listJobsForWorkflowRun) return jobs;
      if (method === rest.issues.listComments) return comments;
      throw new Error("Unexpected API call.");
    },
  };
  const core = {
    notice: (message) => calls.push({ type: "notice", message }),
    summary: { addRaw: () => ({ write: async () => calls.push({ type: "summary" }) }) },
  };
  return {
    calls,
    args: { github, core, context: {
      repo: { owner: "cloudfyy", repo: "qiqiime" }, payload: { repository: { default_branch: "main" } },
    }, issueNumber: 1, recipient, now },
  };
}

test("morning notification creates a mention with time-bounded API queries", async () => {
  const value = fixture();
  await notifyMorningReport(value.args);
  const created = value.calls.find((call) => call.type === "create");
  assert.equal(created.issue_number, 1);
  assert.match(created.body, /@cloudfyy/);
  const reads = value.calls.filter((call) => call.type === "read");
  assert.equal(reads[0].options.created, ">=2026-11-17T19:30:00.000Z");
  assert.equal(reads[0].options.branch, "main");
  assert.equal(reads[2].options.since, "2026-11-17T16:00:00.000Z");
});

test("a repeat reminder edits only today's existing bot report", async () => {
  const marker = "<!-- qiqi-nightly-lint-report:2026-11-18 -->";
  const value = fixture({ comments: [
    { id: 7, user: { login: "someone" }, body: marker },
    { id: 8, user: { login: "github-actions[bot]" }, body: marker },
  ] });
  await notifyMorningReport(value.args);
  assert.equal(value.calls.find((call) => call.type === "update").comment_id, 8);
  assert.equal(value.calls.filter((call) => call.type === "create").length, 0);
});

test("missing report still notifies; closed, misconfigured or unreadable issues do not fake a report", async () => {
  const missing = fixture({ runs: [] });
  await notifyMorningReport(missing.args);
  assert.match(missing.calls.find((call) => call.type === "create").body, /NO RUN FOUND/);
  const closed = fixture({ issue: { body: trackingMarker, state: "closed" } });
  await notifyMorningReport(closed.args);
  assert.deepEqual(closed.calls.map((call) => call.type), ["notice"]);
  const wrong = fixture({ issue: { state: "open", body: "unrelated" } });
  await assert.rejects(() => notifyMorningReport(wrong.args), /not the nightly/);
  const denied = fixture();
  denied.args.github.paginate = async () => { throw new Error("API access denied"); };
  await assert.rejects(() => notifyMorningReport(denied.args), /API access denied/);
  assert.equal(denied.calls.filter((call) => ["create", "update"].includes(call.type)).length, 0);
});

