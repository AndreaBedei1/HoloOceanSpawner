# Seafloor Assets In This Public Extract

This public `HoloOceanSpawner` extract includes two reusable seafloor asset pairs.
Each pair consists of a static mesh and a Blueprint wrapper stored in `engine/Content/`.

## Terrain Seabed Pair

- Preferred English name: `SM_UnderwaterTerrain70m`
  - current package file: `engine/Content/underwater_terrain_70m.uasset`
  - Unreal asset path: `/Game/underwater_terrain_70m.underwater_terrain_70m`
  - type: static mesh
  - purpose: terrain-style underwater seabed mesh

- Preferred English name: `BP_UnderwaterTerrain70m`
  - current package file: `engine/Content/prova_fondo2.uasset`
  - Unreal asset path: `/Game/prova_fondo2.prova_fondo2`
  - type: Blueprint
  - purpose: Blueprint wrapper around `SM_UnderwaterTerrain70m`

## Surface Base Pair

- Preferred English name: `SM_UnderwaterSurface`
  - current package file: `engine/Content/underwater_surface.uasset`
  - Unreal asset path: `/Game/underwater_surface.underwater_surface`
  - type: static mesh
  - purpose: underwater surface base mesh

- Preferred English name: `BP_UnderwaterSurface`
  - current package file: `engine/Content/prova_fondo.uasset`
  - Unreal asset path: `/Game/prova_fondo.prova_fondo`
  - type: Blueprint
  - purpose: Blueprint wrapper around `SM_UnderwaterSurface`

## Usage Notes

- Use the `underwater_terrain_70m` and `underwater_surface` asset paths when a workflow requires a static mesh.
- Treat `prova_fondo2` and `prova_fondo` as Blueprint-side wrappers for Unreal-level environment assembly.
- In documentation and paper text, refer to these assets with the English names above.
- The package filenames in this repository remain unchanged to avoid breaking Unreal binary references.
- If you want the package filenames and Unreal package paths renamed to the English names, do it inside Unreal Editor and fix redirectors there.
