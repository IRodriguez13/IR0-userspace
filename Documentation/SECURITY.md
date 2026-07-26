# Security

> **Last verified:** 2026-07-26

- No lab credentials in `minimal` / `desktop` / `appliance` images  
- `make personal-data-check` / `make rootfs-check`  
- Setuid allowlist enforced at stage time  
- Root locked (`!`) on canonical profiles until firstboot/seed  
