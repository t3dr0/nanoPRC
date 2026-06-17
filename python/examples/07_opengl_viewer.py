#!/usr/bin/env python3
"""OpenGL viewer example for nanoprc_py.

This example loads a PRC/PDF using the nanoprc_py bindings and renders
face geometry with VBO/EBO upload, per-face diffuse material, camera
orbit controls, and tessellation selection.
"""

import math
import sys
from dataclasses import dataclass

import glfw
import numpy as np
from OpenGL.GL import *
from OpenGL.GLU import gluPerspective

from nanoprc_py import Context

PRIMITIVE_MODE_MAP = {
    0: GL_TRIANGLES,
    1: GL_TRIANGLE_FAN,
    2: GL_TRIANGLE_STRIP,
    3: GL_LINES,
    4: GL_LINE_STRIP,
    5: GL_LINE_LOOP,
}

DEFAULT_DIFFUSE = (0.85, 0.85, 0.9, 1.0)
WIREFRAME_COLOR = (0.15, 0.8, 0.95, 1.0)


@dataclass
class Mesh:
    vbo: int
    ebo: int
    index_count: int
    mode: int
    color: tuple[float, float, float, float]
    face_index: int
    primitive_index: int


class NanoPRCViewer:
    def __init__(self, filename: str) -> None:
        self.filename = filename
        self.window = None
        self.width = 1280
        self.height = 800
        self.mouse_down = False
        self.last_cursor = (0.0, 0.0)
        self.yaw = 35.0
        self.pitch = -20.0
        self.distance = 6.0
        self.offset_x = 0.0
        self.offset_y = 0.0
        self.draw_wireframe = False
        self.use_material = True
        self.current_tess = 0
        self.tess_count = 0
        self.meshes: list[Mesh] = []
        self.document = None

    def run(self) -> None:
        self._init_glfw()
        self._load_document()
        self._build_meshes()
        self._main_loop()
        self._cleanup()

    def _init_glfw(self) -> None:
        if not glfw.init():
            raise RuntimeError("Failed to initialize GLFW")

        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 2)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 1)
        glfw.window_hint(glfw.RESIZABLE, True)

        self.window = glfw.create_window(self.width, self.height, "nanoPRC OpenGL Viewer", None, None)
        if not self.window:
            glfw.terminate()
            raise RuntimeError("Failed to create GLFW window")

        glfw.make_context_current(self.window)
        glfw.set_framebuffer_size_callback(self.window, self._on_resize)
        glfw.set_key_callback(self.window, self._on_key)
        glfw.set_mouse_button_callback(self.window, self._on_mouse_button)
        glfw.set_cursor_pos_callback(self.window, self._on_cursor_move)
        glfw.set_scroll_callback(self.window, self._on_scroll)

        glEnable(GL_DEPTH_TEST)
        glEnable(GL_CULL_FACE)
        glCullFace(GL_BACK)
        glClearColor(0.08, 0.08, 0.12, 1.0)

    def _load_document(self) -> None:
        ctx = Context()
        self.document = ctx.open(self.filename)
        self.document.prepare_model_tree()
        self.document.create_model_tree()
        tess_count, _ = self.document.tessellation_counts()
        self.tess_count = int(tess_count)
        if self.tess_count == 0:
            raise RuntimeError("No surface tessellations found in document")
        print(f"Loaded {self.filename}")
        print(f"Surface tessellation count: {self.tess_count}")
        print("Controls:")
        print("  Left drag = rotate")
        print("  Scroll / W/S = zoom")
        print("  Arrow keys = pan")
        print("  T = cycle tessellation")
        print("  M = toggle material colors")
        print("  F = toggle wireframe overlay")
        print("  ESC = exit")

    def _build_meshes(self) -> None:
        self._destroy_meshes()
        self.meshes = []
        print(f"Building meshes for tessellation {self.current_tess}")
        num_faces = int(self.document.number_of_faces(self.current_tess))
        for face_index in range(num_faces):
            positions = np.asarray(self.document.face_vertex_positions(self.current_tess, face_index), dtype=np.float32)
            if positions.size == 0:
                continue

            primitive_count = int(self.document.face_graphics_primitive_count(self.current_tess, face_index))
            if primitive_count == 0:
                continue

            material = self.document.face_material(self.current_tess, face_index)
            diffuse = self._extract_diffuse_color(material)
            color = diffuse if self.use_material else DEFAULT_DIFFUSE

            for primitive_index in range(primitive_count):
                primitive = self.document.get_graphics_primitive(self.current_tess, face_index, primitive_index)
                indices = np.asarray(primitive["indices"], dtype=np.uint32)
                if indices.size == 0:
                    continue

                mode = PRIMITIVE_MODE_MAP.get(int(primitive["type"]), GL_TRIANGLES)
                vbo = glGenBuffers(1)
                ebo = glGenBuffers(1)

                glBindBuffer(GL_ARRAY_BUFFER, vbo)
                glBufferData(GL_ARRAY_BUFFER, positions.nbytes, positions, GL_STATIC_DRAW)

                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo)
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.nbytes, indices, GL_STATIC_DRAW)

                self.meshes.append(Mesh(
                    vbo=vbo,
                    ebo=ebo,
                    index_count=indices.size,
                    mode=mode,
                    color=color,
                    face_index=face_index,
                    primitive_index=primitive_index,
                ))

        glBindBuffer(GL_ARRAY_BUFFER, 0)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0)
        print(f"Created {len(self.meshes)} draw primitives")

    def _destroy_meshes(self) -> None:
        for mesh in self.meshes:
            glDeleteBuffers(1, [mesh.vbo])
            glDeleteBuffers(1, [mesh.ebo])
        self.meshes = []

    def _extract_diffuse_color(self, material: dict) -> tuple[float, float, float, float]:
        if not material or not material.get("is_material", False):
            return DEFAULT_DIFFUSE

        mat = material.get("material", {})
        diffuse = mat.get("diffuse")
        alpha = float(mat.get("diffuse_alpha", 1.0))
        if diffuse is None or len(diffuse) < 3:
            return DEFAULT_DIFFUSE

        try:
            r, g, b = float(diffuse[0]), float(diffuse[1]), float(diffuse[2])
            if alpha == 0.0:
                alpha = 1.0
            return (r, g, b, alpha)
        except Exception:
            return DEFAULT_DIFFUSE

    def _on_resize(self, window, width: int, height: int) -> None:
        self.width = width
        self.height = max(1, height)
        glViewport(0, 0, self.width, self.height)

    def _on_key(self, window, key, scancode, action, mods) -> None:
        if action != glfw.PRESS:
            return

        if key == glfw.KEY_ESCAPE:
            glfw.set_window_should_close(window, True)
        elif key == glfw.KEY_T:
            self.current_tess = (self.current_tess + 1) % self.tess_count
            self._build_meshes()
        elif key == glfw.KEY_M:
            self.use_material = not self.use_material
            self._build_meshes()
        elif key == glfw.KEY_F:
            self.draw_wireframe = not self.draw_wireframe
        elif key == glfw.KEY_W:
            self.distance = max(0.5, self.distance - 0.2)
        elif key == glfw.KEY_S:
            self.distance += 0.2
        elif key == glfw.KEY_UP:
            self.offset_y += 0.1
        elif key == glfw.KEY_DOWN:
            self.offset_y -= 0.1
        elif key == glfw.KEY_LEFT:
            self.offset_x -= 0.1
        elif key == glfw.KEY_RIGHT:
            self.offset_x += 0.1

    def _on_mouse_button(self, window, button, action, mods) -> None:
        if button == glfw.MOUSE_BUTTON_LEFT:
            self.mouse_down = action == glfw.PRESS

    def _on_cursor_move(self, window, xpos: float, ypos: float) -> None:
        if not self.mouse_down:
            self.last_cursor = (xpos, ypos)
            return

        dx = xpos - self.last_cursor[0]
        dy = ypos - self.last_cursor[1]
        self.last_cursor = (xpos, ypos)

        self.yaw += float(dx) * 0.35
        self.pitch += float(dy) * 0.35
        self.pitch = max(-89.9, min(89.9, self.pitch))

    def _on_scroll(self, window, xoffset: float, yoffset: float) -> None:
        self.distance *= 0.92 if yoffset > 0 else 1.08
        self.distance = max(0.5, min(50.0, self.distance))

    def _setup_camera(self) -> None:
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(45.0, self.width / self.height, 0.1, 100.0)

        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()
        glTranslatef(self.offset_x, self.offset_y, -self.distance)
        glRotatef(self.pitch, 1.0, 0.0, 0.0)
        glRotatef(self.yaw, 0.0, 1.0, 0.0)

    def _draw_status(self) -> None:
        title = (
            f"nanoPRC Viewer - tess {self.current_tess + 1}/{self.tess_count} "
            f"| material={'on' if self.use_material else 'off'} "
            f"| wireframe={'on' if self.draw_wireframe else 'off'}"
        )
        glfw.set_window_title(self.window, title)

    def _render(self) -> None:
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        self._setup_camera()

        glEnableClientState(GL_VERTEX_ARRAY)
        glBindBuffer(GL_ARRAY_BUFFER, 0)

        for mesh in self.meshes:
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo)
            glVertexPointer(3, GL_FLOAT, 0, None)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo)

            glColor4f(*mesh.color)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL)
            glDrawElements(mesh.mode, mesh.index_count, GL_UNSIGNED_INT, None)

            if self.draw_wireframe:
                glColor4f(*WIREFRAME_COLOR)
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)
                glLineWidth(1.0)
                glDrawElements(mesh.mode, mesh.index_count, GL_UNSIGNED_INT, None)

        glDisableClientState(GL_VERTEX_ARRAY)
        glBindBuffer(GL_ARRAY_BUFFER, 0)
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0)

    def _main_loop(self) -> None:
        while not glfw.window_should_close(self.window):
            self._draw_status()
            self._render()
            glfw.swap_buffers(self.window)
            glfw.poll_events()

    def _cleanup(self) -> None:
        self._destroy_meshes()
        if self.window:
            glfw.destroy_window(self.window)
        glfw.terminate()


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    if len(argv) != 1:
        print("Usage: python 07_opengl_viewer.py path/to/file.prc")
        return 1

    viewer = NanoPRCViewer(argv[0])
    viewer.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
