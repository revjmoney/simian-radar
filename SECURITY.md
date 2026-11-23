# Security

## Using it safely

Simian Radar is a pocket device that listens to the radio around it. Keep it and anything you save from it (marks, screenshots, logs) to yourself: they can show where you have been and the names of networks and devices near you.

## Reporting a problem

Please report security problems privately, not in a public issue: open the repo's
**Security** tab and choose **Report a vulnerability**
(https://github.com/revjmoney/simian-radar/security/advisories/new).
Say what you found and how to reproduce it. I will reply there.

## What is in place

- Dependabot alerts and security updates
- Secret scanning with push protection
- CodeQL code scanning
- Private vulnerability reporting
- CI on every push and pull request: builds Simian Radar, runs its checks, and fails if
  private data (home addresses, emails, tokens, keys) shows up in the repo

[Rev. J. Money - 2026] · by JMSCNC, makers of RotatoCAM
