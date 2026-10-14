import type * as T from './schema';

export type Handlers = {
    getParameters(): T.WebParameters | Promise<T.WebParameters>;
    getParameterValues(): T.WebParameterSnapshot | Promise<T.WebParameterSnapshot>;
    beginEdit(req: T.WebParameterId): void | Promise<void>;
    edit(req: T.WebParameterEdit): void | Promise<void>;
    endEdit(req: T.WebParameterId): void | Promise<void>;
    editText(req: T.WebParameterText): void | Promise<void>;
    resetParam(req: T.WebParameterId): void | Promise<void>;
    resetAll(): void | Promise<void>;
};

export class UnknownCommandError extends Error
{
    httpStatus = 404;
    constructor(command: string)
    {
        super(`Unknown command: ${command}`);
    }
}

export async function dispatch(handlers: Handlers, command: string, payload: unknown): Promise<unknown>
{
    switch (command)
    {
        case 'getParameters': return await handlers.getParameters();
        case 'getParameterValues': return await handlers.getParameterValues();
        case 'beginEdit': return await handlers.beginEdit(payload as T.WebParameterId);
        case 'edit': return await handlers.edit(payload as T.WebParameterEdit);
        case 'endEdit': return await handlers.endEdit(payload as T.WebParameterId);
        case 'editText': return await handlers.editText(payload as T.WebParameterText);
        case 'resetParam': return await handlers.resetParam(payload as T.WebParameterId);
        case 'resetAll': return await handlers.resetAll();
        default: throw new UnknownCommandError(command);
    }
}
