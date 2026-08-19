# Auditoria técnica do experimento obrigatório — ISSUE-15

## Identidade da revisão

| Campo | Evidência |
| --- | --- |
| Tag congelada | `experiment-v1` |
| Commit | `8af1022a90e804e3d667e7accf41ae01620ad4be` |
| SHA-256 do binário | `52602236f6bbd8091ba5f45d937695e2198bc8744875c5a85b7ce3828ea7d7c0` |
| SHA-256 do executor congelado | `281901a116c21c3ac39ca3136f57e9ac4841737039136dae66acedd2d6423c2c` |
| Configuração | 4 cenários, seeds 1–100, 1.000 processos, custo de troca 1, quantum 4 |
| Início da reexecução auditada | `2026-08-19T02:19:04+00:00` |
| Fim da reexecução auditada | `2026-08-19T02:23:47+00:00` |
| Manifesto final | `results/raw/main/manifest.json` |
| Registro versionável | `results/evidence/experiment-v1-evidence.json` |
| Pacote bruto local | `results/evidence/experiment-v1-raw.tar.gz` |

## Resultado da auditoria técnica

- [x] A tag remota `experiment-v1` aponta para o commit registrado no manifesto.
- [x] O checkout isolado da tag passou em build release, ASan/UBSan, self-tests, testes
  de CLI, oito testes do executor, cinco testes da análise e GCC `-fanalyzer`.
- [x] O PDBH recebe somente `SchedulerProcessView` e `current_time`; não recebe
  `Process *`, rajadas futuras, CPU restante ou chegadas futuras.
- [x] A matriz usa um único workload por cenário/seed e reutiliza seu SHA-256 nos quatro
  algoritmos.
- [x] Todos os resultados registram 1.000 processos, custo de troca 1 e quantum 4.
- [x] A reexecução concluiu 400/400 workloads e 1.600/1.600 resultados válidos.
- [x] Todas as 1.600 execuções terminaram na primeira tentativa, sem falhas ou
  reexecuções.
- [x] O `summary.csv` regenerado foi byte a byte idêntico ao arquivo do PR.
- [x] Os três gráficos regenerados têm pixels, dimensões e DPI idênticos aos arquivos do
  PR; hashes binários podem variar pela codificação PNG do ambiente.
- [x] O pacote bruto determinístico contém exatamente 2.001 arquivos: manifesto, 400
  workloads e 1.600 resultados.
- [x] O ruleset remoto `main-protect` (ID `20650934`) exige o check `build-test` do
  GitHub Actions, além da aprovação obrigatória já existente.
- [ ] O pacote bruto foi anexado a uma GitHub Release da tag `experiment-v1`.
- [ ] Uma segunda pessoa reproduziu e registrou uma amostra independente.

## Hashes publicados

| Artefato | SHA-256 |
| --- | --- |
| `results/consolidated/summary.csv` | `80ed6ec27de369d8887fa952a38e28c7899da1fa0fec2c0fe52c521e7affda02` |
| `results/figures/context_switches_ic95.png` | `575741f11af62a8dba0daa19ea50354bf4b6f6c5e95b6c29bbd19a213f053691` |
| `results/figures/jain_slowdown_pct_ic95.png` | `254f58162be6e9d8d6dded7901a2eec624bdd48028ba1ae36cace2b0f0b5c9f5` |
| `results/figures/mean_turnaround_ic95.png` | `427bc0609c0c85ac45f8a628c7f582ac0f6f95153b5d5255b00d1f6e78e56d92` |
| `experiment-v1-manifest.json` | `c3f1b51ba47dc4274df95973d09a8b7d75e454941163524ca7abbfa8f56e44e8` |
| `experiment-v1-raw.tar.gz` | `43e210a9d7e51575e7dfb9e5581bf2ebc95aafc2d41dcf1e7b43be956abd66f9` |

## Limites e pendências externas

O manifesto desta reexecução foi preservado depois da conclusão do lote. Portanto, ele
não deve ser apresentado retroativamente como um manifesto commitado antes da execução
original. O estado agora é verificável e está pronto para commit e publicação, mas a
cronologia histórica continua sendo uma limitação explícita.

As aprovações dos sete integrantes permanecem fora desta correção. A reprodução por uma
segunda pessoa também exige ação e registro de outro integrante. O check `build-test`
está verde no run `32202365297` e passou a ser obrigatório no ruleset remoto
`main-protect` em `2026-08-18T23:32:23.755-03:00`.
