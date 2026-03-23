# Todo

<!-- Any todo item that has insufficient detail should be ignored. Do these in order! -->

- [ ] Generate a plane with many vertices. Using simplex noise (web search for best implementation), create basic terrain. Make sure the scale of the object is large.
- [ ] Add a sensible UI for creating new objects as well as manipulating existing ones.

---

<!-- Move item here when completed, reference the hash of this commit -->

- [X] Add support for multiple objects using the same buffer for data, so that all geometry can be drawn in a single drawcall
- [X] Add UI scaling
- [X] Add object picking via ID render pass with Pickable component
- [X] Entity hover highlight (emissive tint) + click-to-select + tabbed inspector UI