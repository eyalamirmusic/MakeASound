import type * as T from './schema';

export type Handlers = {
    getParameters(): T.WebParameters | Promise<T.WebParameters>;
    beginEdit(req: T.WebParameterId): void | Promise<void>;
    edit(req: T.WebParameterEdit): void | Promise<void>;
    endEdit(req: T.WebParameterId): void | Promise<void>;
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
        case 'beginEdit': return await handlers.beginEdit(payload as T.WebParameterId);
        case 'edit': return await handlers.edit(payload as T.WebParameterEdit);
        case 'endEdit': return await handlers.endEdit(payload as T.WebParameterId);
        default: throw new UnknownCommandError(command);
    }
}
