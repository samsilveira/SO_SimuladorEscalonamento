# Resultados

- `raw/`: saídas grandes de execução. Fica fora do Git e pode ser regenerado por cenário, seed e commit.
- `consolidated/`: tabelas consolidadas usadas no artigo. Deve ser versionado.
- `figures/`: gráficos finais usados no artigo e apresentação. Deve ser versionado.

Registre no manifesto experimental os comandos, seeds, configurações, hash do commit e hashes das cargas/resultados para manter reprodutibilidade.

Os arquivos brutos de uma execução são CSV. O resultado agregado possui uma linha identificada por `run_id` e `workload_sha256`; métricas por processo são opcionais. Arquivos com extensão `.json` ficam reservados ao manifesto experimental da ISSUE-13.
