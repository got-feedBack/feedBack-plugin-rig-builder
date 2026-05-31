"""Backend core package for rig_builder — shared runtime modules.

Lifted out of the flat plugin root in the v1.3.2 restructure. routes.py
imports these with absolute paths (e.g. `from rb_core.tone3000_client import
Tone3000Client`); the plugin dir is on sys.path via the host loader.
"""
