"""Desk-side analysis helpers that read the committed JSONL captures.

JSONL stays the canonical, lossless record (never edited). Everything in this
package derives *rebuildable* indexes/summaries from it — see `jsonl_sqlite`,
which flattens captures into a queryable SQLite database for time-aligned
meter-to-meter reconciliation.
"""
