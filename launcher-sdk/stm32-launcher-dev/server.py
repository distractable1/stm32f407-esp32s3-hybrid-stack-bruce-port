"""Local MCP tools for creating T100 launcher-compatible application projects."""
import os
from pathlib import Path
import threading

from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations
from app_factory import AppFactory

server=FastMCP('STM32 Launcher Dev')
operation=threading.RLock()
apps_root=Path(os.environ.get('STM32_LAUNCHER_APPS',str(Path.home()/'Documents'/'Codex'/'STM32LauncherApps')))
factory=AppFactory(apps_root)
read=ToolAnnotations(readOnlyHint=True,destructiveHint=False,openWorldHint=False)
write=ToolAnnotations(readOnlyHint=False,destructiveHint=False,openWorldHint=False)

@server.tool(annotations=read)
def status()->dict:
    """Report the launcher ABI, template, managed apps directory, and installed build tools."""
    with operation:return factory.status()

@server.tool(annotations=read)
def list_apps()->dict:
    """List managed launcher app projects without connecting to or changing the board."""
    with operation:return factory.list_apps()

@server.tool(annotations=write)
def create_app(app_id:str,name:str)->dict:
    """Create a new app from the tested template. IDs use 1–8 uppercase letters, digits or underscore, starting with a letter. Existing projects are preserved."""
    with operation:return factory.create_app(app_id,name)

@server.tool(annotations=read)
def validate_app(app_id:str)->dict:
    """Check an app's board/ABI, linker map, startup and protected board-support files. This is a compatibility check, not proof of arbitrary app behavior."""
    with operation:return factory.validate_app(app_id)

@server.tool(annotations=write)
def build_app(app_id:str,configuration:str='Debug')->dict:
    """Clean-build a managed app in an isolated CubeIDE workspace and verify its actual ELF/BIN memory layout. Does not program hardware."""
    with operation:return factory.build_app(app_id,configuration)

@server.tool(annotations=write)
def package_app(app_id:str)->dict:
    """Produce a checked SD .APP package from a successful build whose source and artifact hashes still match. Does not program hardware."""
    with operation:return factory.package_app(app_id)

if __name__=='__main__':server.run(transport='stdio')
