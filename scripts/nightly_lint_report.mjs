// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

export const trackingMarker = "<!-- qiqi-nightly-lint-tracking -->";
const runName = "Nightly lint report";
const jobNames = ["Lint (x64)", "Lint (x86)"];

export function reportWindow(now) {
  const end = new Date(now);
  if (!Number.isFinite(end.getTime())) throw new Error("Invalid report time.");
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: "Asia/Shanghai", year: "numeric", month: "2-digit", day: "2-digit",
  }).formatToParts(end);
  const part = (type) => parts.find((value) => value.type === type).value;
  const day = `${part("year")}-${part("month")}-${part("day")}`;
  return {
    day, end,
    start: new Date(`${day}T03:30:00+08:00`),
    midnight: new Date(`${day}T00:00:00+08:00`),
  };
}

export function selectNightlyRun(runs, now, branch) {
  const { start, end } = reportWindow(now);
  if (!Array.isArray(runs)) throw new Error("Expected workflow runs.");
  return runs.filter((run) => {
    if (run.display_title !== runName || run.head_branch !== branch ||
        !["schedule", "workflow_dispatch"].includes(run.event)) return false;
    const time = Date.parse(run.created_at);
    if (!Number.isFinite(time)) throw new Error("Invalid nightly run creation time.");
    return time >= start.getTime() && time <= end.getTime();
  }).sort((left, right) =>
    Date.parse(right.created_at) - Date.parse(left.created_at) || right.id - left.id)[0] ?? null;
}

export function renderReport({ run, jobs = [], now, repository, recipient }) {
  if (!/^[\w.-]+\/[\w.-]+$/.test(repository) || !/^[\w-]{1,39}$/.test(recipient)) {
    throw new Error("Invalid reminder destination.");
  }
  if (!Array.isArray(jobs)) throw new Error("Expected workflow jobs.");
  const { day } = reportWindow(now);
  const lines = [
    `<!-- qiqi-nightly-lint-report:${day} -->`,
    `## Nightly lint report - ${day} (UTC+8)`,
    "",
    `@${recipient} Morning check-in: please review the nightly lint report.`,
    "",
  ];
  if (!run) {
    lines.push(
      "**NO RUN FOUND** for today's 03:30 nightly window. This is not a pass.",
      "Check whether the workflow is enabled, delayed, or disabled after repository inactivity.",
      `[Open workflow](https://github.com/${repository}/actions/workflows/nightly-lint.yml)`,
    );
    return lines.join("\n");
  }
  if (!Number.isSafeInteger(run.id) || run.id <= 0 || !/^[a-f0-9]{40}$/i.test(run.head_sha)) {
    throw new Error("Invalid nightly run identity.");
  }
  const measured = jobNames.map((name) => {
    const matching = jobs.filter((job) => job.name === name);
    if (matching.length !== 1) return { name, result: "MISSING OR AMBIGUOUS" };
    const job = matching[0];
    if (job.status !== "completed") return { name, result: "NOT COMPLETED" };
    const conclusions = ["success", "failure", "cancelled", "skipped", "timed_out",
      "action_required", "neutral", "stale", "startup_failure"];
    return { name, result: conclusions.includes(job.conclusion) ? job.conclusion : "UNKNOWN" };
  });
  let result;
  if (run.status !== "completed") result = "IN PROGRESS";
  else if (run.conclusion !== "success") result = "FAILED OR NOT SUCCESSFUL";
  else if (measured.every((job) => job.result === "success")) result = "PASS";
  else result = "INCOMPLETE - NOT A PASS";
  lines.push(
    `**${result}**`,
    `- Commit: \`${run.head_sha}\``,
    ...measured.map((job) => `- ${job.name}: **${job.result}**`),
    "",
    `[Open run, full logs and downloadable reports](https://github.com/${repository}/actions/runs/${run.id})`,
    "",
    "Report artifacts are retained for 14 days. Lint is not unit-test, coverage, or installer certification.",
  );
  return lines.join("\n");
}

export async function notifyMorningReport({ github, context, core, issueNumber, recipient, now = new Date() }) {
  if (!Number.isSafeInteger(issueNumber) || issueNumber <= 0) throw new Error("Invalid tracking issue.");
  const repo = context.repo;
  const { data: issue } = await github.rest.issues.get({ ...repo, issue_number: issueNumber });
  if (issue.pull_request || !issue.body?.includes(trackingMarker)) {
    throw new Error("The configured issue is not the nightly lint tracking issue.");
  }
  if (issue.state !== "open") {
    core.notice("Tracking issue is closed; morning reminder comments are paused.");
    return;
  }
  const window = reportWindow(now);
  const runs = await github.paginate(github.rest.actions.listWorkflowRuns, {
    ...repo, workflow_id: "nightly-lint.yml",
    branch: context.payload.repository.default_branch,
    created: `>=${window.start.toISOString()}`, per_page: 100,
  });
  const run = selectNightlyRun(runs, now, context.payload.repository.default_branch);
  const jobs = run ? await github.paginate(github.rest.actions.listJobsForWorkflowRun, {
    ...repo, run_id: run.id, filter: "latest", per_page: 100,
  }) : [];
  const body = renderReport({ run, jobs, now, repository: `${repo.owner}/${repo.repo}`, recipient });
  const marker = `<!-- qiqi-nightly-lint-report:${window.day} -->`;
  const comments = await github.paginate(github.rest.issues.listComments, {
    ...repo, issue_number: issueNumber, since: window.midnight.toISOString(), per_page: 100,
  });
  const previous = comments.find((comment) =>
    comment.user?.login === "github-actions[bot]" && comment.body?.includes(marker));
  if (previous) {
    await github.rest.issues.updateComment({ ...repo, comment_id: previous.id, body });
  } else {
    await github.rest.issues.createComment({ ...repo, issue_number: issueNumber, body });
  }
  await core.summary.addRaw(body).write();
}
